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
	float FlightTime;
	float LaunchTimeSeconds = 0.0f; // wall-clock at launch. All jump timing derives from this, NOT
	                                // from a per-frame accumulator — so it is immune to the movement
	                                // code running more than once per frame (the 2x-time / half-arc bug).
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
		// During a navlink jump, let the capsule pass THROUGH world geometry (the target
		// platform's vertical face). Otherwise it rams the edge, horizontal velocity is
		// killed and the NPC rides straight up the wall ("5:0") instead of arcing onto
		// the platform. Landing is detected by reaching the destination (see
		// UpdatePathSegment), not by ground contact, so dropping world collision is safe.
		const ECollisionResponse R = bEnableCollision ? ECR_Block : ECR_Overlap;
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, R);
		Capsule->SetCollisionResponseToChannel(ECC_WorldStatic, R);
		Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, R);
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
		// Drive the arc off WALL-CLOCK time since launch, NOT an accumulated counter. The base
		// path-following tick and our UpdatePathSegment can both reach this in the same frame; an
		// accumulator (JumpTimeElapsed += DeltaTime) advanced at 2x and cut the jump to HALF its real
		// flight (the undershoot bug: landZ ~= half, dist ~= half). Wall-clock time is identical no
		// matter how many times this runs per frame, so the override below is now idempotent.
		FJumpArcData* ArcData = ActiveJumps.Find(this);
		ACharacter* Character = GetCharacterFromOwner(this);

		if (ArcData && Character)
		{
			const float T = static_cast<float>(GetWorld()->GetTimeSeconds() - ArcData->LaunchTimeSeconds);
			JumpTimeElapsed = T; // kept in sync for the debug logs only (not used for timing decisions)

			// Force velocity to the parabola derivative V(t) = V0 + g*t. World collision is OFF for the
			// whole flight (see ExecuteJump), so this can't fight collision sweeps — it just makes the
			// NPC follow the computed arc exactly regardless of air control / RVO / lateral friction.
			if (UCharacterMovementComponent* MC = Character->GetCharacterMovement())
			{
				MC->Velocity = ArcData->LaunchVelocity + FVector(0.0f, 0.0f, ArcData->EffectiveGravityZ * T);
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
		// Re-assert the arc. FollowPathSegment is now idempotent (wall-clock based), so calling it
		// here is safe even though the base tick also calls it — it just re-sets the same velocity.
		// We still SKIP Super::UpdatePathSegment so the base class can't abort the move mid-jump.
		FollowPathSegment(GetWorld()->GetDeltaSeconds());

		ACharacter* Character = GetCharacterFromOwner(this);
		const FJumpArcData* Arc = ActiveJumps.Find(this);
		// Wall-clock age of the jump — independent of how many times the tick fires per frame.
		const float AirAge = Arc ? static_cast<float>(GetWorld()->GetTimeSeconds() - Arc->LaunchTimeSeconds) : 0.0f;
		const float Flight = Arc ? Arc->FlightTime : MaxJumpDuration;

		// Landing: world collision is OFF during flight (capsule passes through the platform's
		// vertical face). Land when the NPC reaches the (capsule-center-lifted) destination, or the
		// parabola's flight time elapses; LandCharacter restores collision so CMC settles it on top.
		if (Character && AirAge >= LandingCheckDelay)
		{
			const float DistToDest = FVector::Dist(Character->GetActorLocation(), JumpDestination);
			if (DistToDest <= JumpLandingTolerance || AirAge >= Flight)
			{
				// [NAV_DEBUG] Outcome: REACHED the destination (good) or FLIGHT time elapsed while
				// still short. On success expect airAge ~= flight (NOT half) and landZ ~= destZ.
				const FVector LandPos = Character->GetActorLocation();
				UE_LOG(LogTemp, Warning, TEXT("[NAV_DEBUG] %s JUMP LAND cond=%s airAge=%.2f/flight=%.2f dist=%.0f landZ=%.0f destZ=%.0f"),
					GetOwner() ? *GetOwner()->GetName() : TEXT("???"),
					(DistToDest <= JumpLandingTolerance) ? TEXT("REACHED") : TEXT("FLIGHT_ELAPSED"),
					AirAge, Flight, DistToDest, LandPos.Z, JumpDestination.Z);
				LandCharacter(Character);
				CompleteJump();
				return;
			}
		}

		// Timeout safety (wall-clock).
		if (AirAge >= MaxJumpDuration)
		{
			UE_LOG(LogTemp, Warning, TEXT("[NAV_DEBUG] %s JUMP TIMEOUT after %.2fs (MaxJumpDuration=%.1f)"),
				GetOwner() ? *GetOwner()->GetName() : TEXT("???"),
				AirAge, MaxJumpDuration);
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

	// Explicit-apex arc: the jump peaks just above the HIGHER endpoint by JumpApexClearance,
	// so the trajectory is always "only as high as the obstacle needs" — never a comedic
	// double-height lob, and never so flat it clips the edge. This fixes both the silly arc
	// height AND the slow/missed jumps (minimal arc = short flight time + accurate landing,
	// so NPCs don't hesitate/retry).
	const float Gravity = FMath::Max(FMath::Abs(EffectiveGravityZ), 1.0f); // positive magnitude

	// LANDING GEOMETRY: the NavLink end point sits at ~platform_surface + a few uu, but LaunchCharacter
	// moves the capsule CENTER, which rests at surface + capsule_half_height when standing. Aim the arc
	// at the lifted center target so an UP jump leaves the NPC standing ON the platform instead of ~half
	// a capsule embedded in it (which then reads as "short" and fails). Uses the live half-height
	// (crouch/shrink aware) with a sane fallback.
	float CapsuleHalfHeight = 88.0f;
	if (const UCapsuleComponent* Cap = Character->GetCapsuleComponent())
	{
		CapsuleHalfHeight = Cap->GetScaledCapsuleHalfHeight();
	}
	const FVector AimPos = EndPos + FVector(0.0f, 0.0f, CapsuleHalfHeight);

	const FVector Delta = AimPos - StartPos;
	const float ApexZ = FMath::Max(StartPos.Z, AimPos.Z) + FMath::Max(JumpApexClearance, 10.0f);

	const float RiseFromStart = FMath::Max(ApexZ - StartPos.Z, 10.0f);
	const float VzUp = FMath::Sqrt(2.0f * Gravity * RiseFromStart);  // launch speed to reach apex
	const float TimeUp = VzUp / Gravity;
	const float DropToEnd = FMath::Max(ApexZ - AimPos.Z, 10.0f);
	const float TimeDown = FMath::Sqrt(2.0f * DropToEnd / Gravity);
	const float FlightTime = FMath::Max(TimeUp + TimeDown, 0.1f);

	FVector LaunchVelocity;
	LaunchVelocity.X = Delta.X / FlightTime;
	LaunchVelocity.Y = Delta.Y / FlightTime;
	LaunchVelocity.Z = VzUp;
	const bool bFoundArc = true; // explicit solution always valid

	// Set jump state — JumpDestination is the LIFTED capsule-center target (used by the landing check).
	bIsPerformingJump = true;
	JumpDestination = AimPos;
	JumpTimeElapsed = 0.0f;

	// Store arc data. LaunchTimeSeconds = wall-clock now → all jump timing derives from real elapsed
	// time, immune to FollowPathSegment/UpdatePathSegment running more than once per frame.
	ActiveJumps.Add(this, FJumpArcData{ LaunchVelocity, EffectiveGravityZ, FlightTime, static_cast<float>(GetWorld()->GetTimeSeconds()) });

	// Disable EMF during jump — EM forces interfere with the arc
	SetEMFEnabled(Character, false);

	// Let the capsule pass through world geometry for the flight so the platform edge
	// can't kill horizontal velocity. Deliberately NOT calling StopActiveMovement — that
	// zeroed the run-up and made the NPC pause/hop in place right before the jump.
	SetPawnCollisionDuringJump(Character, false);

	// Launch — sets MOVE_Falling and initial velocity
	Character->LaunchCharacter(LaunchVelocity, true, true);

	// [NAV_DEBUG] Diagnostics: launch parameters. Lets us see the requested arc (Vz, flight time)
	// and compare against how the jump actually resolves (LAND / ABORTED / TIMEOUT below).
	{
		const FVector HorizDelta(Delta.X, Delta.Y, 0.0f);
		UE_LOG(LogTemp, Warning, TEXT("[NAV_DEBUG] %s JUMP LAUNCH V=(%.0f,%.0f,%.0f) Vz=%.0f flight=%.2f HDist=%.0f VDelta=%.0f g=%.0f"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("???"),
			LaunchVelocity.X, LaunchVelocity.Y, LaunchVelocity.Z,
			VzUp, FlightTime, HorizDelta.Size(), Delta.Z, EffectiveGravityZ);
	}

#if WITH_EDITOR
	if (bDebugJumps)
	{
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

	// Restore world collision (dropped for the flight) and EMF
	SetPawnCollisionDuringJump(Character, true);
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
		// [NAV_DEBUG] SMOKING GUN: the path finished (abort / new request / stop) WHILE a jump was
		// in the air. If t is tiny here (~0.0-0.2s), the jump was killed on launch by a repath from
		// the StateTree (StopMovement) — that's the "weak hop". flags decode the reason.
		ACharacter* JumpChar = GetCharacterFromOwner(this);
		const float DistToDest = JumpChar ? FVector::Dist(JumpChar->GetActorLocation(), JumpDestination) : -1.0f;
		UE_LOG(LogTemp, Warning, TEXT("[NAV_DEBUG] %s JUMP ABORTED by OnPathFinished t=%.2f dist=%.0f flags=%u"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("???"),
			JumpTimeElapsed, DistToDest, Result.Flags);

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
		UE_LOG(LogTemp, Warning, TEXT("[NAV_DEBUG] %s JUMP CANCELLED externally t=%.2f"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("???"), JumpTimeElapsed);

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
