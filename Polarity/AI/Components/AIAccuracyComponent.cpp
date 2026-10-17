// AIAccuracyComponent.cpp

#include "AIAccuracyComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Curves/CurveFloat.h"
#include "Kismet/KismetMathLibrary.h"
#include "../../ApexMovementComponent.h"
#include "Variant_Shooter/Bench/FightRecorder.h"
#include "AI/AimTargetInterface.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Coop/CoopPlayers.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

UAIAccuracyComponent::UAIAccuracyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

FVector UAIAccuracyComponent::CalculateAimDirection(const FVector& TargetLocation, AActor* Target, float ExtraMissDegrees)
{
	const FVector AimOrigin = GetAimOrigin();
	const FVector BaseDirection = (TargetLocation - AimOrigin).GetSafeNormal();
	bLastRoundDeliberateMiss = false;
	bLastRoundPlaced = false;

	// Suppression: donut pattern — shots always miss but fly close to target. Under fire discipline
	// being suppressed is stress instead (fewer aimed rounds, fewer of them land), so the damage a
	// shooter can do stays the sum it is meant to be. @see ApplySuppression
	if (bIsSuppressed && !bFireDiscipline)
	{
		LastCalculatedSpread = MinSuppressionSpread; // debug: show min angle
		bLastRoundDeliberateMiss = true;
		return KeepClearOfBody(ApplyDonutSpread(BaseDirection, MinSuppressionSpread, MaxSuppressionSpread),
			TargetLocation, Target, ExtraMissDegrees);
	}

	// Deliberate suppressive fire. Checked AFTER being suppressed, not before: an NPC that is itself
	// under fire shoots worse than it means to, and the wider ring is the honest answer for that. The
	// two states are unrelated and can both be true.
	if (bSuppressiveFire || bPinFire)
	{
		LastCalculatedSpread = MinSuppressiveFireSpread;
		bLastRoundDeliberateMiss = true;
		return KeepClearOfBody(ApplyDonutSpreadClearOfOwnCover(BaseDirection, MinSuppressiveFireSpread, MaxSuppressiveFireSpread),
			TargetLocation, Target, ExtraMissDegrees);
	}

	// A target that says how hard it is to hit (the working microwave grenade): its roll, not ours.
	// Placed like a disciplined round, so the weapon adds no cone on top.
	if (const IAimTargetInterface* const Says = Cast<IAimTargetInterface>(Target))
	{
		const float Chance = Says->GetAimHitChanceOverride();
		if (Chance >= 0.0f)
		{
			bLastRoundPlaced = true;
			LastCalculatedSpread = 0.0f;
			if (FMath::FRand() < Chance)
			{
				return BaseDirection;
			}
			bLastRoundDeliberateMiss = true;
			return MakeDeliberateMiss(TargetLocation, Target, ExtraMissDegrees);
		}
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

	// Fire discipline. Not reacted yet, or not this round's turn to be aimed: it passes close and
	// frightens. An aimed one lands or not by a roll, not by a cone. @see UpdateSight
	if (bFireDiscipline && Target->IsA<APawn>())
	{
		bLastRoundPlaced = true;

		// He is not in view this moment (gone behind the corner a breath ago): the round goes where
		// he was, into the cover. Not an aimed round, so it does not spend one. Sight is reported
		// every 0.1 s, hence the margin.
		constexpr float InViewMargin = 0.25f;
		if (!IsReactionDone(Target) || Now - LastSightTime > InViewMargin)
		{
			LastCalculatedSpread = 0.0f;
			bLastRoundDeliberateMiss = true;
			return MakeDeliberateMiss(TargetLocation, Target, ExtraMissDegrees);
		}

		// One that was holding the place he showed at: the first rounds are all aimed.
		const bool bVolley = Now < VolleyUntil;
		if (!bVolley && AimedCredit < 1.0f)
		{
			const float Share = GetTuning().AimedShare * FMath::Lerp(1.0f, StressAimedShareMultiplier, GetStress());
			AimedCredit += Share * FMath::FRandRange(1.0f - AimedRoundJitter, 1.0f + AimedRoundJitter);
		}
		if (!bVolley && AimedCredit < 1.0f)
		{
			LastCalculatedSpread = 0.0f;
			bLastRoundDeliberateMiss = true;
			return MakeDeliberateMiss(TargetLocation, Target, ExtraMissDegrees);
		}
		if (!bVolley)
		{
			AimedCredit -= 1.0f;
		}

		const float Range = static_cast<float>(FVector::Dist(TargetLocation, AimOrigin));
		const float Chance = GetHitChance(Target, Range);
		const bool bLands = FMath::FRand() < Chance;

		// [AIM_ROLL] temporary: every aimed round, to compare the rolls with what landed.
		UE_LOG(LogTemp, Log, TEXT("[AIM_ROLL] %s t=%.2f range=%.0f chance=%.2f lands=%d volley=%d stress=%.2f"),
			*GetNameSafe(GetOwner()), Now, Range, Chance, bLands ? 1 : 0, bVolley ? 1 : 0, GetStress());

		if (!bReactionAnnounced)
		{
			bReactionAnnounced = true;
			static const TCHAR* const Readiness[] = { TEXT("calm"), TEXT("alert"), TEXT("holding") };
			const FString Note = FString::Printf(TEXT("FIRST AIMED ROUND; in view %.2f s; reaction %.2f s (%s, %.0f deg off); stress %.2f; chance %.2f%s"),
				Now - SightSince, SightReaction, Readiness[FMath::Min<uint8>(SightReadiness, 2)], SightAngle, GetStress(), Chance,
				HasHeadache() ? TEXT("; HEADACHE") : TEXT(""));
			UFightRecorderSubsystem::NoteTactic(GetOwner(), FName(*Note));
			UE_LOG(LogTemp, Log, TEXT("[AIM_DISCIPLINE] %s at %s: %s"), *GetNameSafe(GetOwner()), *GetNameSafe(Target), *Note);
		}

		LastCalculatedSpread = 0.0f;
		if (!bLands)
		{
			// Aimed and missed. The record counts it as aimed: it was not meant to miss.
			return MakeDeliberateMiss(TargetLocation, Target, ExtraMissDegrees);
		}
		return MakeHit(TargetLocation, Range);
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
	LastWatchTime = Now;
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

	AddStress(StressFromSuppression);

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

// ==================== Fire discipline ====================

const FAIExperienceTuning& UAIAccuracyComponent::GetTuning() const
{
	switch (Experience)
	{
	case EAIExperience::Rookie:
		return RookieTuning;
	case EAIExperience::Veteran:
		return VeteranTuning;
	default:
		return RegularTuning;
	}
}

void UAIAccuracyComponent::UpdateSight(AActor* Target, bool bInView)
{
	bFireDiscipline = true;

	const UWorld* const World = GetWorld();
	if (!World || !Target)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();

	// Somebody else now: whatever was seen of the last one says nothing about this one.
	if (SightTarget.Get() != Target)
	{
		SightTarget = Target;
		SightSince = -1000.0f;
		LastSightTime = -1000.0f;
	}

	if (!bInView)
	{
		return;
	}

	// Out of view long enough for it to be a new sighting: the reaction starts over.
	if (Now - LastSightTime > SightLossResetSeconds)
	{
		SightSince = Now;
		SightReaction = ComputeReactionForNewSight(Now, Target, SightReadiness, SightAngle);
		bReactionAnnounced = false;
		AimedCredit = 1.0f;
		VolleyUntil = SightReadiness == 2 ? Now + SightReaction + FirstVolleySeconds : -1000.0f;
	}
	LastSightTime = Now;

	// Somebody in view: alert, once the reaction to him above has been decided.
	NoteAlert();
}

float UAIAccuracyComponent::ComputeReactionForNewSight(float Now, const AActor* Target, uint8& OutReadiness, float& OutAngle) const
{
	float Reaction = GetReactionSeconds();
	OutReadiness = 0;
	OutAngle = 0.0f;

	// How far off where its eyes are he shows.
	if (const APawn* const Self = Cast<APawn>(GetOwner()); Self && Target)
	{
		const FVector Eyes = Self->GetBaseAimRotation().Vector().GetSafeNormal2D();
		const FVector ToHim = (Target->GetActorLocation() - GetAimOrigin()).GetSafeNormal2D();
		if (!Eyes.IsNearlyZero() && !ToHim.IsNearlyZero())
		{
			OutAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Eyes, ToHim)), -1.0f, 1.0f)));
		}
	}

	// Holding: alerted a moment ago or the gun held on him, and he shows where it looks.
	const bool bFresh = !HasHeadache() && Now - FMath::Max(LastAlertTime, LastWatchTime) <= HoldFreshSeconds;
	if (bFresh && OutAngle <= HoldConeDegrees)
	{
		OutReadiness = 2;
		return FMath::Min(Reaction, HoldReactionSeconds * FMath::Lerp(1.0f, StressReactionMultiplier, GetStress()));
	}

	if (IsAlert())
	{
		OutReadiness = 1;
		Reaction *= AlertReactionScale;
	}

	// It was holding its gun on the place he went down behind: he comes up into a waiting aim.
	if (Now - LastWatchTime <= 0.5f)
	{
		Reaction *= WatchedReactionScale;
	}

	// Looking elsewhere: it has to turn to him first.
	const float Off = FMath::Clamp((OutAngle - HoldConeDegrees) / FMath::Max(90.0f - HoldConeDegrees, 1.0f), 0.0f, 1.0f);
	return Reaction + TurnReactionSeconds * Off;
}

float UAIAccuracyComponent::GetHitChance(const AActor* Target, float Range) const
{
	const FAIExperienceTuning& Tuning = GetTuning();

	// Dodging is moving across the line of fire; straight at the shooter or away is no harder to hit.
	float Across = 0.0f;
	if (Target)
	{
		const FVector Line = (Target->GetActorLocation() - GetAimOrigin()).GetSafeNormal2D();
		const FVector Velocity = Target->GetVelocity();
		const FVector Flat(Velocity.X, Velocity.Y, 0.0f);
		Across = static_cast<float>((Flat - Line * FVector::DotProduct(Flat, Line)).Size());
	}
	float Chance = FMath::Lerp(Tuning.HitChanceStill, Tuning.HitChanceDodging,
		FMath::Clamp(Across / DodgeReferenceSpeed, 0.0f, 1.0f));

	// Further off, less of it. Linear between the two ranges.
	if (HitChanceFarRange > HitChanceFullRange)
	{
		const float Far = FMath::Clamp((Range - HitChanceFullRange) / (HitChanceFarRange - HitChanceFullRange), 0.0f, 1.0f);
		Chance *= FMath::Lerp(1.0f, FarHitChanceScale, Far);
	}

	// The same things that widened the cone make the roll harder, by as much: a target in the air
	// or on a wall, the shooter on the move, a light in its eyes.
	float State = 1.0f;
	if (IsTargetWallRunning(const_cast<AActor*>(Target)))
	{
		State = FMath::Max(State, WallRunSpreadMultiplier);
	}
	if (IsTargetInAir(const_cast<AActor*>(Target)))
	{
		State = FMath::Max(State, InAirSpreadMultiplier);
	}
	Chance /= State * GetSelfMovementSpreadMultiplier() * DazzleSpreadMultiplier;

	Chance *= FMath::Lerp(1.0f, StressHitChanceMultiplier, GetStress());
	return FMath::Clamp(Chance, 0.0f, 1.0f);
}

FVector UAIAccuracyComponent::MakeHit(const FVector& TargetLocation, float Range) const
{
	const FVector Origin = GetAimOrigin();
	const FVector Forward = (TargetLocation - Origin).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector;
	}

	// A point on the torso around the aim point, never off it.
	FVector Right, Up;
	Forward.FindBestAxisVectors(Right, Up);
	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const float Offset = HitPlacementRadius * FMath::Sqrt(FMath::FRand());
	const FVector Point = TargetLocation + (Right * FMath::Cos(Angle) + Up * FMath::Sin(Angle)) * Offset;
	return (Point - Origin).GetSafeNormal();
}

float UAIAccuracyComponent::GetReactionSeconds() const
{
	return GetTuning().ReactionSeconds
		* FMath::Lerp(1.0f, StressReactionMultiplier, GetStress())
		* DeviceReactionMultiplier
		* (HasHeadache() ? HeadacheReactionMultiplier : 1.0f);
}

void UAIAccuracyComponent::NotePainted(float Seconds)
{
	const UWorld* const World = GetWorld();
	if (!World || Seconds <= 0.0f)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	if (Now >= PaintedUntil)
	{
		PaintedSince = Now;
	}
	PaintedUntil = FMath::Max(PaintedUntil, Now + Seconds);
}

bool UAIAccuracyComponent::IsPainted() const
{
	const UWorld* const World = GetWorld();
	return World && World->GetTimeSeconds() < PaintedUntil;
}

float UAIAccuracyComponent::GetPaintedSeconds() const
{
	const UWorld* const World = GetWorld();
	return IsPainted() && World ? World->GetTimeSeconds() - PaintedSince : 0.0f;
}

void UAIAccuracyComponent::NoteAlert()
{
	const UWorld* const World = GetWorld();
	if (World && !HasHeadache())
	{
		LastAlertTime = World->GetTimeSeconds();
	}
}

bool UAIAccuracyComponent::IsAlert() const
{
	const UWorld* const World = GetWorld();
	return World && !HasHeadache() && World->GetTimeSeconds() - LastAlertTime <= AlertSeconds;
}

bool UAIAccuracyComponent::HasHeadache() const
{
	const UWorld* const World = GetWorld();
	return World && HeadacheStrength > 0.0f && World->GetTimeSeconds() < HeadacheUntil;
}

void UAIAccuracyComponent::ApplyHeadache(float Strength, float Seconds)
{
	const UWorld* const World = GetWorld();
	if (!World || Strength <= 0.0f || Seconds <= 0.0f)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();

	// The stronger of the two, for as long as the longer one. Renewed by every pulse of an emitter.
	const bool bFresh = !HasHeadache();
	HeadacheStrength = FMath::Clamp(bFresh ? Strength : FMath::Max(HeadacheStrength, Strength), 0.0f, 1.0f);
	HeadacheUntil = FMath::Max(HeadacheUntil, Now + Seconds);
	if (!bFresh)
	{
		return;
	}

	// It comes on: whatever it was holding and whatever it had seen, gone. Once, not at every pulse,
	// or a soldier under an emitter would never finish a reaction.
	LastAlertTime = -1000.0f;
	LastWatchTime = -1000.0f;
	VolleyUntil = -1000.0f;
	RestartReaction();

	UFightRecorderSubsystem::NoteTactic(GetOwner(), FName(*FString::Printf(TEXT("HEADACHE %.2f for %.1f s"), HeadacheStrength, Seconds)));
}

int32 UAIAccuracyComponent::ApplyHeadacheInRadius(const UObject* WorldContext, FVector Center, float Radius, float Strength, float Seconds)
{
	UWorld* const World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_Client || Radius <= 0.0f)
	{
		return 0;
	}

	int32 Count = 0;
	for (TActorIterator<AShooterNPC> It(World); It; ++It)
	{
		AShooterNPC* const NPC = *It;
		UAIAccuracyComponent* const Accuracy = NPC ? NPC->FindComponentByClass<UAIAccuracyComponent>() : nullptr;
		if (!Accuracy || NPC->IsDead() || FVector::DistSquared(NPC->GetActorLocation(), Center) > FMath::Square(Radius))
		{
			continue;
		}
		Accuracy->ApplyHeadache(Strength, Seconds);
		++Count;
	}
	return Count;
}

// polarity.ai.Headache [radius cm] [seconds] [strength]: a stress grenade at the local player's feet.
static FAutoConsoleCommandWithWorldAndArgs GCmdAIHeadache(
	TEXT("polarity.ai.Headache"),
	TEXT("Headache to every soldier around the local player, walls or not. Args: radius cm (1500), seconds (8), strength 0-1 (0.8)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const APlayerController* const Controller = CoopPlayers::GetLocalController(World);
		const APawn* const Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!Pawn)
		{
			return;
		}
		const float Radius = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 1500.0f;
		const float Seconds = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 8.0f;
		const float Strength = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.8f;
		const int32 Count = UAIAccuracyComponent::ApplyHeadacheInRadius(World, Pawn->GetActorLocation(), Radius, Strength, Seconds);
		UE_LOG(LogTemp, Log, TEXT("[AIM_DISCIPLINE] headache %.2f for %.1f s on %d soldiers within %.0f cm"), Strength, Seconds, Count, Radius);
	}));

bool UAIAccuracyComponent::IsReactionDone(const AActor* Target) const
{
	if (!bFireDiscipline || !Target || !Target->IsA<APawn>())
	{
		return true;
	}
	if (SightTarget.Get() != Target)
	{
		return false;
	}

	const UWorld* const World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	// Firing at where he was, not at him, for longer than a glance away: nothing is meant to hit.
	if (Now - LastSightTime > SightLossResetSeconds)
	{
		return false;
	}
	return Now - SightSince >= SightReaction;
}

float UAIAccuracyComponent::GetStress() const
{
	const UWorld* const World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	// Whole for StressHoldSeconds after the last event, then fading over StressFadeSeconds.
	const float Age = Now - LastStressEventTime;
	float FromEvents = EventStress;
	if (Age > StressHoldSeconds)
	{
		FromEvents *= FMath::Clamp(1.0f - (Age - StressHoldSeconds) / StressFadeSeconds, 0.0f, 1.0f);
	}

	const float Stress = FMath::Clamp(FromEvents + StressFromWounds * WoundFraction, 0.0f, 1.0f);

	// A headache is stress that does not fade while it lasts.
	return HasHeadache() ? FMath::Max(Stress, HeadacheStrength) : Stress;
}

void UAIAccuracyComponent::AddStress(float Amount)
{
	const UWorld* const World = GetWorld();
	if (!World || Amount <= 0.0f)
	{
		return;
	}

	// What is left of the last events, plus this one, held whole from now.
	const float Wounds = StressFromWounds * WoundFraction;
	const float FromEventsNow = FMath::Max(0.0f, GetStress() - Wounds);
	EventStress = FMath::Clamp(FromEventsNow + Amount * GetTuning().StressGainScale, 0.0f, 1.0f);
	LastStressEventTime = World->GetTimeSeconds();
}

void UAIAccuracyComponent::SetDazzleSpreadMultiplier(float Multiplier)
{
	DazzleSpreadMultiplier = FMath::Max(1.0f, Multiplier);

	const bool bDazzled = DazzleSpreadMultiplier > 1.01f;
	if (bDazzled && DeviceReactionMultiplier <= 1.0f)
	{
		// A light in the eyes: whatever it had seen of the target, it has to find him again.
		DeviceReactionMultiplier = DazzleReactionMultiplier;
		LastSightTime = -1000.0f;
	}
	else if (!bDazzled)
	{
		DeviceReactionMultiplier = 1.0f;
	}
}

FVector UAIAccuracyComponent::MakeDeliberateMiss(const FVector& TargetLocation, const AActor* Target, float ExtraMissDegrees) const
{
	const FVector Origin = GetAimOrigin();
	const FVector ToAim = TargetLocation - Origin;
	const float Range = FMath::Max(static_cast<float>(ToAim.Size()), 50.0f);
	const FVector Forward = ToAim.GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector;
	}

	FVector Right, Up;
	Forward.FindBestAxisVectors(Right, Up);

	// The body as its capsule: a round has to pass outside this, not outside the point it was aimed at.
	float Radius = 40.0f;
	float HalfHeight = 90.0f;
	FVector BodyCentre = TargetLocation;
	if (Target)
	{
		Target->GetSimpleCollisionCylinder(Radius, HalfHeight);
		BodyCentre = Target->GetActorLocation();
	}
	const FVector AxisTop = BodyCentre + FVector(0.0f, 0.0f, FMath::Max(HalfHeight - Radius, 0.0f));
	const FVector AxisBottom = BodyCentre - FVector(0.0f, 0.0f, FMath::Max(HalfHeight - Radius, 0.0f));

	const float MustClear = GetMissMustClear(Range, Radius, ExtraMissDegrees);

	const UWorld* const World = GetWorld();
	FCollisionQueryParams Params(TEXT("AIMissOwnCover"), false, GetOwner());
	const FCollisionShape Probe = FCollisionShape::MakeSphere(25.0f);

	auto PassDistance = [&](const FVector& Direction)
	{
		return GetMissPassDistance(Direction, Range, AxisTop, AxisBottom);
	};

	for (int32 Try = 0; Try < 8; ++Try)
	{
		const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
		const FVector Out = Right * FMath::Cos(Angle) + Up * FMath::Sin(Angle);
		const float Clearance = FMath::FRandRange(MissClearanceMin, FMath::Max(MissClearanceMin, MissClearanceMax));

		// Straight out of the aim point by the clearance, then pushed further until the whole line
		// passes outside the body by it: going down from the chest, the legs are still in the way.
		FVector Direction = ((TargetLocation + Out * (MustClear + Clearance)) - Origin).GetSafeNormal();
		const float Pass = PassDistance(Direction);
		if (Pass < MustClear + Clearance * 0.5f)
		{
			Direction = ((TargetLocation + Out * (MustClear + Clearance + (MustClear + Clearance - Pass))) - Origin).GetSafeNormal();
			if (PassDistance(Direction) < MustClear + 10.0f)
			{
				continue;
			}
		}

		// Not into the corner the shooter is leaning round.
		if (World && OwnCoverClearDistance > 0.0f)
		{
			FHitResult Hit;
			if (World->SweepSingleByChannel(Hit, Origin + Direction * 40.0f, Origin + Direction * OwnCoverClearDistance,
				FQuat::Identity, ECC_Visibility, Probe, Params))
			{
				continue;
			}
		}
		return Direction;
	}

	// Nothing on the ring was clear: over the head, which is open whenever the straight line is.
	const FVector OverHead = AxisTop + FVector(0.0f, 0.0f, MustClear + MissClearanceMin);
	return (OverHead - Origin).GetSafeNormal();
}

float UAIAccuracyComponent::GetMissMustClear(float Range, float BodyRadius, float ExtraMissDegrees) const
{
	// A pellet pattern is laid around the direction after it is chosen: the miss widens by it.
	const float PatternSlack = Range * FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(ExtraMissDegrees, 0.0f, 30.0f)));
	return BodyRadius + MissBodyMargin + PatternSlack;
}

float UAIAccuracyComponent::GetMissPassDistance(const FVector& Direction, float Range, const FVector& AxisTop, const FVector& AxisBottom) const
{
	// The closest any line the round can take comes to the body: from the eye, and from the muzzle
	// towards wherever the eye's ray ends (just behind the target, or far off).
	const FVector Origin = GetAimOrigin();
	auto LineToAxis = [&](const FVector& From, const FVector& To)
	{
		FVector OnLine, OnAxis;
		FMath::SegmentDistToSegmentSafe(From, To, AxisTop, AxisBottom, OnLine, OnAxis);
		return static_cast<float>(FVector::Dist(OnLine, OnAxis));
	};

	float Pass = LineToAxis(Origin, Origin + Direction * (Range + 500.0f));
	if (bHasShotOrigin)
	{
		Pass = FMath::Min(Pass, LineToAxis(ShotOrigin, Origin + Direction * (Range + 300.0f)));
		Pass = FMath::Min(Pass, LineToAxis(ShotOrigin, Origin + Direction * 10000.0f));
	}
	return Pass;
}

FVector UAIAccuracyComponent::KeepClearOfBody(const FVector& Direction, const FVector& TargetLocation, const AActor* Target, float ExtraMissDegrees) const
{
	// The rings above are in degrees: at range they pass well wide, at two metres a few degrees is
	// still the body. A ring round that lands in him is replaced by a miss measured at the body.
	// Recorded on the CORNER bench 2026-10-14: a suppressed soldier's "misses" were a third of the hits.
	const APawn* const Body = Cast<APawn>(Target);
	if (!Body)
	{
		return Direction;
	}

	float Radius = 40.0f;
	float HalfHeight = 90.0f;
	Body->GetSimpleCollisionCylinder(Radius, HalfHeight);
	const FVector Centre = Body->GetActorLocation();
	const FVector AxisTop = Centre + FVector(0.0f, 0.0f, FMath::Max(HalfHeight - Radius, 0.0f));
	const FVector AxisBottom = Centre - FVector(0.0f, 0.0f, FMath::Max(HalfHeight - Radius, 0.0f));
	const float Range = FMath::Max(static_cast<float>(FVector::Dist(GetAimOrigin(), TargetLocation)), 50.0f);

	if (GetMissPassDistance(Direction, Range, AxisTop, AxisBottom) >= GetMissMustClear(Range, Radius, ExtraMissDegrees) + MissClearanceMin * 0.5f)
	{
		return Direction;
	}
	return MakeDeliberateMiss(TargetLocation, Target, ExtraMissDegrees);
}
