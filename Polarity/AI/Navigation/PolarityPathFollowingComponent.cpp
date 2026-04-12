// PolarityPathFollowingComponent.cpp
// Custom path following that handles NavLink traversal via character jumps

#include "PolarityPathFollowingComponent.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshPath.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "EMFVelocityModifier.h"

// ==================== Arc Data (cpp-only, no header bloat) ====================

struct FJumpArcData
{
	FVector LaunchVelocity;
	float EffectiveGravityZ;
};

static TMap<UPolarityPathFollowingComponent*, FJumpArcData> ActiveJumps;

// ==================== Helpers ====================

static void SetEMFEnabled(ACharacter* Character, bool bEnabled)
{
	if (!Character) return;
	if (UEMFVelocityModifier* EMF = Character->FindComponentByClass<UEMFVelocityModifier>())
	{
		EMF->SetEnabled(bEnabled);
	}
}

static void SetPawnCollisionDuringJump(ACharacter* Character, bool bEnableCollision)
{
	if (!Character) return;
	if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, bEnableCollision ? ECR_Block : ECR_Overlap);
	}
}

static ACharacter* GetCharacterFromOwner(UActorComponent* Component)
{
	AAIController* AI = Cast<AAIController>(Component->GetOwner());
	return AI ? Cast<ACharacter>(AI->GetPawn()) : nullptr;
}

// ==================== Constructor ====================

UPolarityPathFollowingComponent::UPolarityPathFollowingComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// ==================== PathFollowingComponent Overrides ====================

void UPolarityPathFollowingComponent::SetMoveSegment(int32 SegmentStartIndex)
{
	Super::SetMoveSegment(SegmentStartIndex);

	if (bIsPerformingJump)
	{
		return;
	}

	if (!Path.IsValid() || !Path->IsValid())
	{
		return;
	}

	const TArray<FNavPathPoint>& PathPoints = Path->GetPathPoints();
	const int32 NextIndex = SegmentStartIndex + 1;

	if (!PathPoints.IsValidIndex(SegmentStartIndex) || !PathPoints.IsValidIndex(NextIndex))
	{
		return;
	}

	const FNavPathPoint& CurrentPoint = PathPoints[SegmentStartIndex];

	bool bIsNavLink = false;
	FNavMeshNodeFlags NodeFlags(CurrentPoint.Flags);
	if (NodeFlags.IsNavLink())
	{
		bIsNavLink = true;
	}
	if (!bIsNavLink && CurrentPoint.CustomNavLinkId.IsValid())
	{
		bIsNavLink = true;
	}

	// [NAV_DEBUG] Log every segment to see if NavLinks appear in paths at all
	UE_LOG(LogTemp, Warning, TEXT("[NAV_DEBUG] %s SetMoveSegment %d/%d Flags=%u CustomLinkId=%s IsNavLink=%s Pos=(%.0f,%.0f,%.0f)"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("???"),
		SegmentStartIndex, PathPoints.Num() - 1,
		CurrentPoint.Flags,
		CurrentPoint.CustomNavLinkId.IsValid() ? TEXT("VALID") : TEXT("none"),
		bIsNavLink ? TEXT("YES") : TEXT("NO"),
		CurrentPoint.Location.X, CurrentPoint.Location.Y, CurrentPoint.Location.Z);

	if (bIsNavLink)
	{
		const FVector& JumpEnd = PathPoints[NextIndex].Location;

		UE_LOG(LogTemp, Warning, TEXT("[NAV_DEBUG] %s JUMP TRIGGERED seg %d -> (%.0f, %.0f, %.0f)"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("???"),
			SegmentStartIndex,
			JumpEnd.X, JumpEnd.Y, JumpEnd.Z);

		ExecuteJump(JumpEnd);
	}
}

void UPolarityPathFollowingComponent::FollowPathSegment(float DeltaTime)
{
	if (bIsPerformingJump)
	{
		JumpTimeElapsed += DeltaTime;

		// Force velocity to match parabola derivative: V(t) = V0 + g*t
		// CMC is in MOVE_Falling and will try to modify velocity via collision sweeps.
		// We override it every frame so the NPC follows the calculated arc exactly.
		FJumpArcData* ArcData = ActiveJumps.Find(this);
		ACharacter* Character = GetCharacterFromOwner(this);

		if (ArcData && Character)
		{
			if (UCharacterMovementComponent* MC = Character->GetCharacterMovement())
			{
				const float T = JumpTimeElapsed;
				FVector DesiredVelocity = ArcData->LaunchVelocity + FVector(0.0f, 0.0f, ArcData->EffectiveGravityZ * T);
				MC->Velocity = DesiredVelocity;
			}
		}

		return;
	}

	Super::FollowPathSegment(DeltaTime);
}

void UPolarityPathFollowingComponent::UpdatePathSegment()
{
	if (bIsPerformingJump)
	{
		// Explicitly call FollowPathSegment — base UpdatePathSegment would do this
		// but we skip Super to prevent it from aborting movement during jump
		FollowPathSegment(GetWorld()->GetDeltaSeconds());

		// Check for landing: CMC detects ground normally since we're in MOVE_Falling
		if (JumpTimeElapsed >= LandingCheckDelay)
		{
			ACharacter* Character = GetCharacterFromOwner(this);
			if (Character)
			{
				UCharacterMovementComponent* MC = Character->GetCharacterMovement();
				if (MC && MC->IsMovingOnGround())
				{
					LandCharacter(Character);
					CompleteJump();
					return;
				}
			}
		}

		// Timeout safety
		if (JumpTimeElapsed >= MaxJumpDuration)
		{
#if WITH_EDITOR
			if (bDebugJumps)
			{
				UE_LOG(LogTemp, Warning, TEXT("[%s] Jump timed out after %.1fs"),
					GetOwner() ? *GetOwner()->GetName() : TEXT("???"),
					JumpTimeElapsed);
			}
#endif
			LandCharacter(GetCharacterFromOwner(this));
			CompleteJump();
			return;
		}

		return;
	}

	Super::UpdatePathSegment();
}

// ==================== Same-Level LoS Check ====================

// Check if there's a reachable position on the NPC's current level
// from which it can see (shoot) the target. If yes — no need to jump.
static bool HasShootingPositionOnSameLevel(UWorld* World, AAIController* AIOwner, ACharacter* Character)
{
	// Get combat target from AI focus
	AActor* Target = AIOwner->GetFocusActor();
	if (!Target) return false; // No target — allow jump (exploring/patrolling)

	const FVector MyLoc = Character->GetActorLocation();
	const FVector TargetLoc = Target->GetActorLocation();
	const FVector EyeOffset(0.0f, 0.0f, 80.0f);

	FCollisionQueryParams TraceParams;
	TraceParams.AddIgnoredActor(Character);

	// Quick check: do I have LoS right now?
	if (!World->LineTraceTestByChannel(MyLoc + EyeOffset, TargetLoc + EyeOffset, ECC_Visibility, TraceParams))
	{
		return true; // Already have LoS — stay on this level
	}

	// Sample reachable NavMesh points on same level, check LoS from each
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys) return false;

	constexpr float SearchRadius = 3000.0f;
	constexpr float HeightTolerance = 200.0f; // Same "level" = within 2m vertically
	constexpr int32 NumSamples = 20;

	for (int32 i = 0; i < NumSamples; ++i)
	{
		FNavLocation SamplePoint;
		if (NavSys->GetRandomReachablePointInRadius(MyLoc, SearchRadius, SamplePoint))
		{
			// Skip points on different height levels
			if (FMath::Abs(SamplePoint.Location.Z - MyLoc.Z) > HeightTolerance)
			{
				continue;
			}

			// Check LoS from this point to target
			if (!World->LineTraceTestByChannel(SamplePoint.Location + EyeOffset, TargetLoc + EyeOffset, ECC_Visibility, TraceParams))
			{
				return true; // Found a same-level position with LoS
			}
		}
	}

	return false; // No viable position on this level — jumping is necessary
}

// ==================== Jump Execution ====================

void UPolarityPathFollowingComponent::ExecuteJump(const FVector& EndPos)
{
	AAIController* AIOwner = Cast<AAIController>(GetOwner());
	if (!AIOwner) return;

	// Hard override — NavLink jumps manually blocked
	if (!bAllowNavLinkJumps)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NAV_DEBUG] %s JUMP BLOCKED: bAllowNavLinkJumps=false"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("???"));
		AIOwner->GetPathFollowingComponent()->AbortMove(*AIOwner, FPathFollowingResultFlags::MovementStop);
		return;
	}

	ACharacter* Character = Cast<ACharacter>(AIOwner->GetPawn());
	if (!Character) return;

	UE_LOG(LogTemp, Warning, TEXT("[NAV_DEBUG] %s JUMP EXECUTING to (%.0f,%.0f,%.0f)"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("???"),
		EndPos.X, EndPos.Y, EndPos.Z);

	const FVector StartPos = Character->GetActorLocation();

	// Effective gravity = world gravity * character's GravityScale
	float EffectiveGravityZ = GetWorld()->GetGravityZ();
	if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
	{
		EffectiveGravityZ *= MoveComp->GravityScale;
	}

	// Calculate launch velocity for parabolic arc
	FVector LaunchVelocity;
	bool bFoundArc = UGameplayStatics::SuggestProjectileVelocity_CustomArc(
		GetWorld(), LaunchVelocity, StartPos, EndPos, EffectiveGravityZ, JumpArcParam);

	if (!bFoundArc)
	{
		const FVector Delta = EndPos - StartPos;
		const float Gravity = FMath::Abs(EffectiveGravityZ);
		const float HorizontalDist = FVector(Delta.X, Delta.Y, 0.0f).Size();
		const float FlightTime = FMath::Max(0.5f, HorizontalDist / 600.0f);

		LaunchVelocity.X = Delta.X / FlightTime;
		LaunchVelocity.Y = Delta.Y / FlightTime;
		LaunchVelocity.Z = (Delta.Z + 0.5f * Gravity * FlightTime * FlightTime) / FlightTime;

		UE_LOG(LogTemp, Warning, TEXT("[%s] SuggestProjectileVelocity failed, using fallback"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("???"));
	}

	// Set jump state
	bIsPerformingJump = true;
	JumpDestination = EndPos;
	JumpTimeElapsed = 0.0f;

	// Store arc data for per-frame velocity override
	ActiveJumps.Add(this, FJumpArcData{ LaunchVelocity, EffectiveGravityZ });

	// Stop current movement, then launch
	if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
	{
		MoveComp->StopActiveMovement();
	}

	// Disable EMF during jump — EM forces interfere with the arc
	SetEMFEnabled(Character, false);

	// Launch — sets MOVE_Falling and initial velocity
	Character->LaunchCharacter(LaunchVelocity, true, true);

#if WITH_EDITOR
	if (bDebugJumps)
	{
		const FVector Delta = EndPos - StartPos;
		const FVector HorizDelta(Delta.X, Delta.Y, 0.0f);
		const float DebugDuration = 5.0f;

		DrawDebugSphere(GetWorld(), StartPos, 20.0f, 8, FColor::Green, false, DebugDuration);
		DrawDebugSphere(GetWorld(), EndPos, 20.0f, 8, FColor::Red, false, DebugDuration);
		DrawDebugLine(GetWorld(), StartPos, EndPos, FColor::Yellow, false, DebugDuration, 0, 2.0f);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Cyan,
				FString::Printf(TEXT("[%s] JUMP: Vel=(%.0f,%.0f,%.0f) HDist=%.0f VDist=%.0f Arc=%s"),
					GetOwner() ? *GetOwner()->GetName() : TEXT("???"),
					LaunchVelocity.X, LaunchVelocity.Y, LaunchVelocity.Z,
					HorizDelta.Size(), Delta.Z,
					bFoundArc ? TEXT("OK") : TEXT("FALLBACK")));
		}
	}
#endif
}

// ==================== Landing & Cleanup ====================

void UPolarityPathFollowingComponent::LandCharacter(ACharacter* Character)
{
	if (!Character) return;

	// Restore EMF
	SetEMFEnabled(Character, true);
}

bool UPolarityPathFollowingComponent::HasCharacterLanded() const
{
	ACharacter* Character = GetCharacterFromOwner(const_cast<UPolarityPathFollowingComponent*>(this));
	if (!Character) return true;

	UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();
	if (!MoveComp) return true;

	return MoveComp->IsMovingOnGround();
}

void UPolarityPathFollowingComponent::CompleteJump()
{
	bIsPerformingJump = false;
	ActiveJumps.Remove(this);

#if WITH_EDITOR
	if (bDebugJumps)
	{
		ACharacter* Character = GetCharacterFromOwner(this);
		if (Character && GEngine)
		{
			const FVector LandPos = Character->GetActorLocation();
			const float DistToTarget = FVector::Dist(LandPos, JumpDestination);
			const FVector HorizError(LandPos.X - JumpDestination.X, LandPos.Y - JumpDestination.Y, 0.0f);
			const float VertError = LandPos.Z - JumpDestination.Z;
			const bool bSuccess = DistToTarget <= JumpLandingTolerance;

			GEngine->AddOnScreenDebugMessage(-1, 5.0f, bSuccess ? FColor::Green : FColor::Red,
				FString::Printf(TEXT("[%s] JUMP %s: Dist=%.0f (H=%.0f V=%.0f) Time=%.2fs"),
					GetOwner() ? *GetOwner()->GetName() : TEXT("???"),
					bSuccess ? TEXT("OK") : TEXT("MISS"),
					DistToTarget, HorizError.Size(), VertError, JumpTimeElapsed));
		}
	}
#endif
}

void UPolarityPathFollowingComponent::OnPathFinished(const FPathFollowingResult& Result)
{
	if (bIsPerformingJump)
	{
		LandCharacter(GetCharacterFromOwner(this));
		CompleteJump();

#if WITH_EDITOR
		if (bDebugJumps)
		{
			UE_LOG(LogTemp, Log, TEXT("[%s] Jump reset — path finished (flags: %d)"),
				GetOwner() ? *GetOwner()->GetName() : TEXT("???"), Result.Flags);
		}
#endif
	}

	Super::OnPathFinished(Result);
}

void UPolarityPathFollowingComponent::CancelJump()
{
	if (bIsPerformingJump)
	{
		LandCharacter(GetCharacterFromOwner(this));
		CompleteJump();

#if WITH_EDITOR
		if (bDebugJumps)
		{
			UE_LOG(LogTemp, Log, TEXT("[%s] Jump cancelled externally"),
				GetOwner() ? *GetOwner()->GetName() : TEXT("???"));
		}
#endif
	}
}
