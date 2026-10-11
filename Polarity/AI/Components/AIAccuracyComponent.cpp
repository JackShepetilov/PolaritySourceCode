// AIAccuracyComponent.cpp

#include "AIAccuracyComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Curves/CurveFloat.h"
#include "Kismet/KismetMathLibrary.h"
#include "../../ApexMovementComponent.h"

UAIAccuracyComponent::UAIAccuracyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

FVector UAIAccuracyComponent::CalculateAimDirection(const FVector& TargetLocation, AActor* Target)
{
	const FVector AimOrigin = GetAimOrigin();
	const FVector BaseDirection = (TargetLocation - AimOrigin).GetSafeNormal();

	// Suppression: donut pattern — shots always miss but fly close to target
	if (bIsSuppressed)
	{
		LastCalculatedSpread = MinSuppressionSpread; // debug: show min angle
		return ApplyDonutSpread(BaseDirection, MinSuppressionSpread, MaxSuppressionSpread);
	}

	// Deliberate suppressive fire. Checked AFTER being suppressed, not before: an NPC that is itself
	// under fire shoots worse than it means to, and the wider ring is the honest answer for that. The
	// two states are unrelated and can both be true.
	if (bSuppressiveFire || bPinFire)
	{
		LastCalculatedSpread = MinSuppressiveFireSpread;
		return ApplyDonutSpreadClearOfOwnCover(BaseDirection, MinSuppressiveFireSpread, MaxSuppressiveFireSpread);
	}

	if (!Target)
	{
		// No target, use base spread only
		LastCalculatedSpread = BaseSpread;
		LastSpeedRatio = 0.0f;
		return ApplySpreadToDirection(BaseDirection, BaseSpread);
	}

	// The string of fire. Only aimed rounds count: the rings above return before this, so an NPC that
	// was pinning and then gets its aimed slot starts a fresh string, warning round included.
	const UWorld* const World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	const bool bNewString = SettleTarget.Get() != Target || Now - LastAimTime > AimSettleResetSeconds;
	if (bNewString)
	{
		SettleTarget = Target;
		SettleStartTime = Now;
	}
	LastAimTime = Now;

	if (bNewString && bFirstShotMisses)
	{
		LastCalculatedSpread = MinSuppressiveFireSpread;
		return ApplyDonutSpreadClearOfOwnCover(BaseDirection, MinSuppressiveFireSpread, MaxSuppressiveFireSpread);
	}

	// Calculate spread based on target state
	float Spread = GetCurrentSpread(Target) * GetSettleSpreadMultiplier();

	// Past SpreadFullRange the miss stays the size it is there. @see SpreadFullRange
	const float Range = static_cast<float>(FVector::Dist(TargetLocation, AimOrigin));
	if (SpreadFullRange > 0.0f && Range > SpreadFullRange)
	{
		Spread *= SpreadFullRange / Range;
	}
	LastCalculatedSpread = Spread;

	return ApplySpreadToDirection(BaseDirection, Spread);
}

float UAIAccuracyComponent::GetCurrentSpread(AActor* Target) const
{
	if (!Target)
	{
		return BaseSpread;
	}

	// Get speed-based spread
	const float SpeedRatio = GetTargetSpeedRatio(Target);

	// Use curve if available, otherwise linear interpolation
	float SpreadFactor;
	if (SpeedToSpreadCurve)
	{
		SpreadFactor = FMath::Clamp(SpeedToSpreadCurve->GetFloatValue(SpeedRatio), 0.0f, 1.0f);
	}
	else
	{
		SpreadFactor = SpeedRatio;
	}

	// Interpolate between base and max spread
	float FinalSpread = FMath::Lerp(BaseSpread, MaxSpread, SpreadFactor);

	// Apply movement state multipliers (use highest multiplier, don't stack)
	float StateMultiplier = 1.0f;

	if (IsTargetWallRunning(Target))
	{
		StateMultiplier = FMath::Max(StateMultiplier, WallRunSpreadMultiplier);
	}

	if (IsTargetInAir(Target))
	{
		StateMultiplier = FMath::Max(StateMultiplier, InAirSpreadMultiplier);
	}

	FinalSpread *= StateMultiplier;

	// And how badly WE are shooting because of what we are doing. Multiplied rather than folded into
	// the max above: that block picks the worst of several descriptions of the same thing (the
	// target's movement), while this is a different thing entirely.
	FinalSpread *= GetSelfMovementSpreadMultiplier();

	// Blinded by a tactical light: firing at where the glare is, not at the person.
	FinalSpread *= DazzleSpreadMultiplier;

	return FinalSpread;
}

void UAIAccuracyComponent::NoteAiming(AActor* Target)
{
	const UWorld* const World = GetWorld();
	if (!World || !Target)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (SettleTarget.Get() != Target || Now - LastAimTime > AimSettleResetSeconds)
	{
		SettleTarget = Target;
		SettleStartTime = Now;
	}
	LastAimTime = Now;
}

float UAIAccuracyComponent::GetSettleSpreadMultiplier() const
{
	if (UnsettledSpreadMultiplier <= 1.0f || AimSettleSeconds <= 0.0f)
	{
		return 1.0f;
	}

	const UWorld* const World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	const float Settled = FMath::Clamp((Now - SettleStartTime) / AimSettleSeconds, 0.0f, 1.0f);

	return FMath::Lerp(UnsettledSpreadMultiplier, 1.0f, Settled);
}

float UAIAccuracyComponent::GetSelfMovementSpreadMultiplier() const
{
	if (SelfMovementSpreadMultiplier <= 1.0f || SelfMovementReferenceSpeed <= 0.0f)
	{
		return 1.0f;
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return 1.0f;
	}

	// 2D on purpose: falling does not make an NPC a worse shot in any way the player can read, and
	// counting it would punish every drop off a ledge.
	const float Speed = Owner->GetVelocity().Size2D();
	const float Ratio = FMath::Clamp(Speed / SelfMovementReferenceSpeed, 0.0f, 1.0f);

	return FMath::Lerp(1.0f, SelfMovementSpreadMultiplier, Ratio);
}

float UAIAccuracyComponent::GetTargetSpeedRatio(AActor* Target) const
{
	if (!Target)
	{
		return 0.0f;
	}

	// Try to get velocity from character movement component first
	if (const ACharacter* Character = Cast<ACharacter>(Target))
	{
		if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			const float Speed = Movement->Velocity.Size();
			const float Ratio = FMath::Clamp(Speed / MaxTargetSpeed, 0.0f, 1.0f);

			// Cache for debugging (const_cast for mutable debug variable)
			const_cast<UAIAccuracyComponent*>(this)->LastSpeedRatio = Ratio;

			return Ratio;
		}
	}

	// Fallback: use actor velocity
	if (Target->GetVelocity().Size() > 0.0f)
	{
		const float Speed = Target->GetVelocity().Size();
		return FMath::Clamp(Speed / MaxTargetSpeed, 0.0f, 1.0f);
	}

	return 0.0f;
}

bool UAIAccuracyComponent::IsTargetWallRunning(AActor* Target) const
{
	if (!Target)
	{
		return false;
	}

	// Check for ApexMovementComponent
	if (const ACharacter* Character = Cast<ACharacter>(Target))
	{
		if (const UApexMovementComponent* ApexMovement = Character->FindComponentByClass<UApexMovementComponent>())
		{
			return ApexMovement->IsWallRunning();
		}
	}

	return false;
}

bool UAIAccuracyComponent::IsTargetInAir(AActor* Target) const
{
	if (!Target)
	{
		return false;
	}

	if (const ACharacter* Character = Cast<ACharacter>(Target))
	{
		if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			return Movement->IsFalling();
		}
	}

	return false;
}

FVector UAIAccuracyComponent::ApplySpreadToDirection(const FVector& BaseDirection, float SpreadDegrees) const
{
	if (SpreadDegrees <= 0.0f)
	{
		return BaseDirection;
	}

	// Sample distribution to bias shots
	const float DistributionSample = SampleSpreadDistribution();

	// Convert to actual angle within the cone
	const float ActualSpread = SpreadDegrees * DistributionSample;

	// Generate random direction within cone
	return UKismetMathLibrary::RandomUnitVectorInConeInDegrees(BaseDirection, ActualSpread);
}

FVector UAIAccuracyComponent::GetAimOrigin() const
{
	if (const AActor* Owner = GetOwner())
	{
		// Try to get eye location for characters
		if (const APawn* Pawn = Cast<APawn>(Owner))
		{
			return Pawn->GetPawnViewLocation();
		}
		return Owner->GetActorLocation();
	}
	return FVector::ZeroVector;
}

float UAIAccuracyComponent::SampleSpreadDistribution() const
{
	const float RandomValue = FMath::FRand();

	if (SpreadDistributionCurve)
	{
		return FMath::Clamp(SpreadDistributionCurve->GetFloatValue(RandomValue), 0.0f, 1.0f);
	}

	// Default: uniform distribution
	return RandomValue;
}

// ==================== Suppression ====================

void UAIAccuracyComponent::ApplySuppression(float Duration, float DiminishingFactor)
{
	if (Duration <= 0.0f)
	{
		return;
	}

	if (bIsSuppressed)
	{
		// Already suppressed: add time with diminishing returns
		// Each stack adds less: Duration / (1 + StackCount * Factor)
		const float DiminishedDuration = Duration / (1.0f + SuppressionStackCount * DiminishingFactor);
		SuppressionTimeRemaining += DiminishedDuration;
		SuppressionStackCount++;

		UE_LOG(LogTemp, Log, TEXT("AIAccuracy: Suppression stacked (x%d). Added %.2fs (base %.2fs). Remaining: %.2fs"),
			SuppressionStackCount, DiminishedDuration, Duration, SuppressionTimeRemaining);
	}
	else
	{
		// Fresh suppression
		bIsSuppressed = true;
		SuppressionTimeRemaining = Duration;
		SuppressionStackCount = 1;
		SetComponentTickEnabled(true);

		UE_LOG(LogTemp, Log, TEXT("AIAccuracy: Suppression started. Duration: %.2fs"), Duration);

		OnSuppressionStart.Broadcast(Duration);
	}
}

void UAIAccuracyComponent::SetSuppressiveFire(bool bEnabled)
{
	if (bSuppressiveFire == bEnabled)
	{
		return;
	}

	bSuppressiveFire = bEnabled;

	UE_LOG(LogTemp, Log, TEXT("[AI_DEBUG] %s suppressive fire %s"),
		*GetNameSafe(GetOwner()), bEnabled ? TEXT("ON") : TEXT("OFF"));
}

void UAIAccuracyComponent::ClearSuppression()
{
	if (!bIsSuppressed)
	{
		return;
	}

	bIsSuppressed = false;
	SuppressionTimeRemaining = 0.0f;
	SuppressionStackCount = 0;
	SetComponentTickEnabled(false);

	UE_LOG(LogTemp, Log, TEXT("AIAccuracy: Suppression ended."));

	OnSuppressionEnd.Broadcast();
}

void UAIAccuracyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bIsSuppressed)
	{
		SuppressionTimeRemaining -= DeltaTime;
		if (SuppressionTimeRemaining <= 0.0f)
		{
			ClearSuppression();
		}
	}
}

FVector UAIAccuracyComponent::ApplyDonutSpreadClearOfOwnCover(const FVector& BaseDirection, float InnerSpread, float OuterSpread) const
{
	const UWorld* const World = GetWorld();
	if (!World || OwnCoverClearDistance <= 0.0f)
	{
		return ApplyDonutSpread(BaseDirection, InnerSpread, OuterSpread);
	}

	FCollisionQueryParams Params(TEXT("AIMissOwnCover"), false, GetOwner());
	const FVector Origin = GetAimOrigin();

	// A fat ray rather than a line: the round leaves the muzzle, which sits lower and to the side of
	// the eyes this is traced from.
	const FCollisionShape Probe = FCollisionShape::MakeSphere(25.0f);

	FVector Direction = BaseDirection;
	for (int32 Try = 0; Try < 6; ++Try)
	{
		Direction = ApplyDonutSpread(BaseDirection, InnerSpread, OuterSpread);

		FHitResult Hit;
		if (!World->SweepSingleByChannel(Hit, Origin + Direction * 40.0f, Origin + Direction * OwnCoverClearDistance,
			FQuat::Identity, ECC_Visibility, Probe, Params))
		{
			return Direction;
		}
	}

	// Every side of the ring is wall: over the target's head, which is the one way that is open
	// whenever the straight line is.
	return (BaseDirection + FVector(0.0f, 0.0f, FMath::Tan(FMath::DegreesToRadians(InnerSpread)))).GetSafeNormal();
}

FVector UAIAccuracyComponent::ApplyDonutSpread(const FVector& BaseDirection, float InnerSpread, float OuterSpread) const
{
	// Random angle between inner and outer — guarantees miss (inner) while keeping shots close (outer)
	const float SpreadAngle = FMath::FRandRange(InnerSpread, OuterSpread);
	const float RandomRotation = FMath::FRandRange(0.0f, 360.0f);

	// Find perpendicular axes to BaseDirection
	FVector Right, Up;
	BaseDirection.FindBestAxisVectors(Right, Up);

	// Random rotation axis around BaseDirection
	const float RotRad = FMath::DegreesToRadians(RandomRotation);
	const FVector RotAxis = Right * FMath::Cos(RotRad) + Up * FMath::Sin(RotRad);

	// Rotate BaseDirection by SpreadAngle degrees around RotAxis perpendicular
	const float SpreadRad = FMath::DegreesToRadians(SpreadAngle);
	const FVector Result = (BaseDirection * FMath::Cos(SpreadRad) + RotAxis * FMath::Sin(SpreadRad)).GetSafeNormal();

	return Result;
}
