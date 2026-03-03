// ShooterWeapon_Melee.cpp

#include "ShooterWeapon_Melee.h"
#include "ShooterWeaponHolder.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/AI/Boss/BossCharacter.h"
#include "Variant_Shooter/DamageTypes/DamageType_MomentumBonus.h"
#include "Variant_Shooter/DamageTypes/DamageType_Dropkick.h"
#include "ShooterDummyInterface.h"
#include "PolarityCharacter.h"
#include "ApexMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Engine/DamageEvents.h"
#include "DrawDebugHelpers.h"

AShooterWeapon_Melee::AShooterWeapon_Melee()
{
	// Disable irrelevant base weapon systems
	bUseHeatSystem = false;
	bUseZFactor = false;
	bUseHitscan = false;
	bUseChargeFiring = false;
	bFullAuto = false; // One press = one swing

	// Melee-appropriate defaults
	RefireRate = 0.4f;
	MagazineSize = 999;
	ShotNoiseRange = 500.0f;
	ShotLoudness = 0.3f;
}

void AShooterWeapon_Melee::BeginPlay()
{
	Super::BeginPlay();

	CurrentBullets = MagazineSize;

	// Cache player controller
	if (PawnOwner)
	{
		CachedPlayerController = Cast<APlayerController>(PawnOwner->GetController());
	}
}

void AShooterWeapon_Melee::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Hit detection during damage window
	if (bDamageWindowActive)
	{
		UpdateDamageWindow();
	}

	// Magnetism and lunge during damage window
	if (bIsMagnetismActive)
	{
		UpdateMagnetism(DeltaTime);
	}

	// Momentum preservation during swing
	if (bDamageWindowActive && bPreserveMomentum)
	{
		UpdateMomentumPreservation(DeltaTime);
	}

	// Cool kick boost (independent of damage window)
	UpdateCoolKick(DeltaTime);

	// Camera focus
	UpdateCameraFocus(DeltaTime);
}

// ==================== Fire ====================

void AShooterWeapon_Melee::Fire()
{
	if (!bIsFiring)
	{
		return;
	}

	// Keep ammo full
	CurrentBullets = MagazineSize;

	// Cache player controller if needed
	if (!CachedPlayerController && PawnOwner)
	{
		CachedPlayerController = Cast<APlayerController>(PawnOwner->GetController());
	}

	// Store velocity at swing start for momentum calculations
	if (PawnOwner)
	{
		if (UCharacterMovementComponent* Movement = PawnOwner->FindComponentByClass<UCharacterMovementComponent>())
		{
			VelocityAtSwingStart = Movement->Velocity;
		}
	}

	// Close previous damage window if still active
	if (bDamageWindowActive)
	{
		DeactivateDamageWindow();
	}

	// Stop previous magnetism
	if (bIsMagnetismActive)
	{
		StopMagnetism();
	}

	// Reset state for new swing
	bHitDuringWindow = false;
	HitActorsThisSwing.Empty();
	LungeProgress = 0.0f;

	// Stop current montage so the new one can play
	StopCurrentMontage();

	// Start magnetism (pre-attack target lock-on)
	StartMagnetism();

	// Select and play swing animation
	CurrentSwingData = SelectWeightedSwing();
	if (CurrentSwingData && CurrentSwingData->SwingMontage)
	{
		WeaponOwner->PlayFiringMontage(CurrentSwingData->SwingMontage);
		PlayMeleeCameraShake(CurrentSwingData->SwingCameraShake, CurrentSwingData->SwingShakeScale);
	}
	else if (FiringMontage)
	{
		WeaponOwner->PlayFiringMontage(FiringMontage);
	}

	// Play swing sound
	PlayMeleeSound(SwingSound);

	// Spawn swing trail VFX
	SpawnSwingTrail();

	// Fire perception event (AI awareness)
	OnShotFired.Broadcast();

	// Update last shot time
	TimeOfLastShot = GetWorld()->GetTimeSeconds();
}

// ==================== Damage Window (AnimNotify API) ====================

void AShooterWeapon_Melee::ActivateDamageWindow()
{
	bDamageWindowActive = true;
	bHitDuringWindow = false;
	HitActorsThisSwing.Empty();
}

void AShooterWeapon_Melee::DeactivateDamageWindow()
{
	bDamageWindowActive = false;

	// Play miss sound if nothing was hit during the window
	if (!bHitDuringWindow)
	{
		PlayMeleeSound(MissSound);

		// Titanfall 2: Restore pre-attack velocity on miss
		if (bPreserveMomentum && PawnOwner)
		{
			if (UCharacterMovementComponent* Movement = PawnOwner->FindComponentByClass<UCharacterMovementComponent>())
			{
				FVector RestoredVelocity = VelocityAtSwingStart * MomentumPreservationRatio;

				// Keep current Z velocity if falling (don't fight gravity)
				if (Movement->IsFalling())
				{
					RestoredVelocity.Z = Movement->Velocity.Z;
				}

				Movement->Velocity = RestoredVelocity;
			}
		}
	}

	// Stop trail VFX
	StopSwingTrail();

	// Stop magnetism and restore state
	StopMagnetism();

	// Stop camera focus
	StopCameraFocus();
}

// ==================== Damage Window Update ====================

void AShooterWeapon_Melee::UpdateDamageWindow()
{
	// Drop kick special case: use distance to target instead of camera trace
	if (bIsDropKick && MagnetismTarget.IsValid() && PawnOwner)
	{
		AActor* Target = MagnetismTarget.Get();
		FVector PlayerPos = PawnOwner->GetActorLocation();
		FVector TargetPos = Target->GetActorLocation();
		float DistanceToTarget = FVector::Dist(PlayerPos, TargetPos);

		if (DistanceToTarget <= AttackRange)
		{
			// Create a hit result for the target
			FHitResult FakeHit;
			FakeHit.ImpactPoint = TargetPos;
			FakeHit.ImpactNormal = (PlayerPos - TargetPos).GetSafeNormal();
			FakeHit.Location = TargetPos;
			FakeHit.bBlockingHit = true;

			if (UPrimitiveComponent* TargetRoot = Cast<UPrimitiveComponent>(Target->GetRootComponent()))
			{
				FakeHit.Component = TargetRoot;
			}

			if (!HitActorsThisSwing.Contains(Target))
			{
				ProcessHit(FakeHit);
			}
		}

		// Don't fall through to normal trace during dropkick
		return;
	}

	// Normal hit detection
	FHitResult HitResult;
	if (PerformMeleeTrace(HitResult))
	{
		AActor* HitActor = HitResult.GetActor();

		// Skip actors already hit during this swing
		if (HitActor && !HitActorsThisSwing.Contains(HitActor))
		{
			ProcessHit(HitResult);
		}
	}
}

// ==================== Process Hit ====================

void AShooterWeapon_Melee::ProcessHit(const FHitResult& HitResult)
{
	AActor* HitActor = HitResult.GetActor();
	if (!HitActor || !PawnOwner)
	{
		return;
	}

	// Mark actor as hit (prevent multi-hit)
	HitActorsThisSwing.Add(HitActor);
	bHitDuringWindow = true;

	// Check for cool kick trigger (first hit, airborne, no magnetism lunge target)
	if (!bHitDuringWindow) // This is the first hit check (before we set it above... but we already set it)
	{
		// Note: bHitDuringWindow was set above, so check HitActorsThisSwing count instead
	}

	// Cool kick: if airborne and this is first hit without magnetism lunge
	if (HitActorsThisSwing.Num() == 1 && !MagnetismTarget.IsValid())
	{
		if (UCharacterMovementComponent* Movement = PawnOwner->FindComponentByClass<UCharacterMovementComponent>())
		{
			if (Movement->IsFalling())
			{
				StartCoolKick();
			}
		}
	}

	// Boss finisher check
	if (ABossCharacter* Boss = Cast<ABossCharacter>(HitActor))
	{
		if (Boss->IsInFinisherPhase())
		{
			Boss->ExecuteFinisher(Cast<ACharacter>(PawnOwner));

			// Play hit effects
			PlayMeleeSound(HitSound);
			PlayMeleeCameraShake(HitCameraShake, HitShakeScale);
			SpawnMeleeImpactFX(HitResult.ImpactPoint, HitResult.ImpactNormal);

			// Report hit to weapon owner
			bool bHeadshot = IsHeadshot(HitResult);
			FVector HitDirection = (HitResult.ImpactPoint - PawnOwner->GetActorLocation()).GetSafeNormal();
			WeaponOwner->OnWeaponHit(HitResult.ImpactPoint, HitDirection, MeleeDamage, bHeadshot, false);
			return;
		}
	}

	// Apply damage (multiple damage types)
	float FinalDamage = ApplyMeleeDamage(HitActor, HitResult);

	// Apply knockback with full momentum system
	FVector ImpulseDirection = (HitResult.ImpactPoint - PawnOwner->GetActorLocation()).GetSafeNormal();
	if (ImpulseDirection.IsNearlyZero())
	{
		// Fallback to camera direction
		FVector CameraLocation;
		FRotator CameraRotation;
		PawnOwner->GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);
		ImpulseDirection = CameraRotation.Vector();
	}
	float FinalImpulse = HitImpulse * CalculateMomentumImpulseMultiplier();

	// Titanfall 2: Add momentum transfer
	if (bTransferMomentumOnHit)
	{
		float VelocityInAttackDir = FVector::DotProduct(VelocityAtSwingStart, ImpulseDirection);
		if (VelocityInAttackDir > 0.0f)
		{
			float MomentumBonus = VelocityInAttackDir * MomentumTransferMultiplier;
			FinalImpulse += MomentumBonus;
		}
	}

	ApplyCharacterImpulse(HitActor, ImpulseDirection, FinalImpulse);

	// Play hit effects
	PlayMeleeSound(HitSound);
	PlayMeleeCameraShake(HitCameraShake, HitShakeScale);
	SpawnMeleeImpactFX(HitResult.ImpactPoint, HitResult.ImpactNormal);

	// Report hit to weapon owner (hit markers, charge gain, etc.)
	bool bHeadshot = IsHeadshot(HitResult);
	FVector HitDirection = (HitResult.ImpactPoint - PawnOwner->GetActorLocation()).GetSafeNormal();
	bool bKilled = HitActor->IsActorBeingDestroyed() || (Cast<AShooterNPC>(HitActor) && Cast<AShooterNPC>(HitActor)->IsDead());
	WeaponOwner->OnWeaponHit(HitResult.ImpactPoint, HitDirection, FinalDamage, bHeadshot, bKilled);
}

// ==================== Hit Detection ====================

bool AShooterWeapon_Melee::PerformMeleeTrace(FHitResult& OutHit)
{
	if (!PawnOwner || !PawnOwner->GetController())
	{
		return false;
	}

	// Get camera location and direction
	FVector CameraLocation;
	FRotator CameraRotation;
	PawnOwner->GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);
	FVector Forward = CameraRotation.Vector();

	// Calculate trace start and end
	FVector TraceStart = CameraLocation + Forward * TraceForwardOffset;
	FVector TraceEnd = TraceStart + Forward * AttackRange;

	// Sphere trace
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(PawnOwner);
	QueryParams.bReturnPhysicalMaterial = false;
	QueryParams.bTraceComplex = false;

	// Add already-hit actors to ignore list
	for (AActor* HitActor : HitActorsThisSwing)
	{
		QueryParams.AddIgnoredActor(HitActor);
	}

	// Use SweepMulti to support cone detection and valid target filtering
	TArray<FHitResult> HitResults;
	bool bHit = GetWorld()->SweepMultiByChannel(
		HitResults,
		TraceStart,
		TraceEnd,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(AttackRadius),
		QueryParams
	);

	if (bHit)
	{
		for (const FHitResult& Hit : HitResults)
		{
			AActor* HitActor = Hit.GetActor();

			if (!HitActor || HitActorsThisSwing.Contains(HitActor))
			{
				continue;
			}

			// Validate target type
			if (!IsValidMeleeTarget(HitActor))
			{
				continue;
			}

			// During drop kick, don't hit IShooterDummyTarget (prevent accidental key hits)
			if (bIsDropKick && HitActor->Implements<UShooterDummyTarget>())
			{
				continue;
			}

			// Cone-based angle check
			if (AttackAngle > 0.0f)
			{
				FVector ToTarget = (Hit.ImpactPoint - TraceStart).GetSafeNormal();
				float Angle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Forward, ToTarget)));

				if (Angle > AttackAngle)
				{
					continue;
				}
			}

			// Valid hit
			OutHit = Hit;
			return true;
		}
	}

	return false;
}

float AShooterWeapon_Melee::ApplyMeleeDamage(AActor* HitActor, const FHitResult& HitResult)
{
	if (!HitActor || !PawnOwner)
	{
		return 0.0f;
	}

	float TotalDamage = 0.0f;
	FVector TraceDir = (HitActor->GetActorLocation() - PawnOwner->GetActorLocation()).GetSafeNormal();
	AController* InstigatorController = PawnOwner->GetController();

	// 1. Base melee damage
	float BaseDamage = MeleeDamage;

	if (IsHeadshot(HitResult))
	{
		BaseDamage *= MeleeHeadshotMultiplier;
	}

	if (BaseDamage > 0.0f)
	{
		FPointDamageEvent BaseDamageEvent(BaseDamage, HitResult, TraceDir, MeleeDamageType);
		HitActor->TakeDamage(BaseDamage, BaseDamageEvent, InstigatorController, PawnOwner);
		TotalDamage += BaseDamage;
	}

	// 2. Momentum bonus damage (separate damage type)
	float MomentumDmg = CalculateMomentumDamage(HitActor);
	if (MomentumDmg > 0.0f)
	{
		FPointDamageEvent MomentumDamageEvent(MomentumDmg, HitResult, TraceDir, UDamageType_MomentumBonus::StaticClass());
		HitActor->TakeDamage(MomentumDmg, MomentumDamageEvent, InstigatorController, PawnOwner);
		TotalDamage += MomentumDmg;
	}

	// 3. Drop kick bonus damage (separate damage type)
	float DropKickDmg = CalculateDropKickBonusDamage();
	if (DropKickDmg > 0.0f)
	{
		FPointDamageEvent DropKickDamageEvent(DropKickDmg, HitResult, TraceDir, UDamageType_Dropkick::StaticClass());
		HitActor->TakeDamage(DropKickDmg, DropKickDamageEvent, InstigatorController, PawnOwner);
		TotalDamage += DropKickDmg;
	}

	return TotalDamage;
}

bool AShooterWeapon_Melee::IsHeadshot(const FHitResult& HitResult) const
{
	FName BoneName = HitResult.BoneName;
	if (BoneName.IsNone())
	{
		return false;
	}

	FString BoneString = BoneName.ToString().ToLower();
	return BoneString.Contains(TEXT("head")) ||
		   BoneString.Contains(TEXT("neck")) ||
		   BoneString.Contains(TEXT("face"));
}

bool AShooterWeapon_Melee::IsValidMeleeTarget(AActor* HitActor) const
{
	if (!HitActor)
	{
		return false;
	}

	// Don't hit ourselves
	if (HitActor == PawnOwner)
	{
		return false;
	}

	// Pawns are valid
	if (Cast<APawn>(HitActor))
	{
		return true;
	}

	// ShooterDummyTarget interface (training dummies)
	if (HitActor->Implements<UShooterDummyTarget>())
	{
		return true;
	}

	// MeleeDestructible tag (destructible environment)
	if (HitActor->ActorHasTag(TEXT("MeleeDestructible")))
	{
		return true;
	}

	return false;
}

float AShooterWeapon_Melee::CalculateMomentumDamage(AActor* HitActor) const
{
	if (MomentumDamagePerSpeed <= 0.0f || !HitActor || !PawnOwner)
	{
		return 0.0f;
	}

	FVector ToTarget = (HitActor->GetActorLocation() - PawnOwner->GetActorLocation()).GetSafeNormal();
	float VelocityTowardTarget = FMath::Max(0.0f, FVector::DotProduct(VelocityAtSwingStart, ToTarget));

	float BonusDamage = (VelocityTowardTarget / 100.0f) * MomentumDamagePerSpeed;
	return FMath::Min(BonusDamage, MaxMomentumDamage);
}

float AShooterWeapon_Melee::CalculateMomentumImpulseMultiplier() const
{
	if (MomentumImpulseMultiplier <= 0.0f)
	{
		return 1.0f;
	}

	float Speed = VelocityAtSwingStart.Size();
	return 1.0f + (Speed * MomentumImpulseMultiplier);
}

float AShooterWeapon_Melee::CalculateDropKickBonusDamage() const
{
	if (!bIsDropKick || DropKickHeightDifference <= 0.0f)
	{
		return 0.0f;
	}

	float BonusDamage = (DropKickHeightDifference / 100.0f) * DropKickDamagePerHeight;
	return FMath::Min(BonusDamage, DropKickMaxBonusDamage);
}

// ==================== Knockback ====================

void AShooterWeapon_Melee::ApplyCharacterImpulse(AActor* HitActor, const FVector& ImpulseDirection, float ImpulseStrength)
{
	if (!HitActor || !PawnOwner)
	{
		return;
	}

	// Calculate knockback direction (center-to-center)
	FVector PlayerCenter = PawnOwner->GetActorLocation();
	FVector TargetCenter = HitActor->GetActorLocation();
	FVector KnockbackDirection = TargetCenter - PlayerCenter;

	if (bIsDropKick)
	{
		// Drop kick: use full 3D direction but clamp upward component
		KnockbackDirection.Normalize();
		if (KnockbackDirection.Z > 0.3f)
		{
			KnockbackDirection.Z = 0.0f;
			KnockbackDirection.Normalize();
		}
	}
	else
	{
		// Normal melee: horizontal knockback only
		KnockbackDirection.Z = 0.0f;
		KnockbackDirection.Normalize();
	}

	// Calculate player speed toward target for distance calculation
	float PlayerSpeedTowardTarget = 0.0f;
	if (!VelocityAtSwingStart.IsNearlyZero())
	{
		PlayerSpeedTowardTarget = FMath::Max(0.0f, FVector::DotProduct(VelocityAtSwingStart, KnockbackDirection));
	}

	// Calculate total knockback distance
	float KnockbackDistance = BaseKnockbackDistance + (PlayerSpeedTowardTarget * KnockbackDistancePerVelocity);

	// Calculate duration proportional to distance
	float KnockbackDur = KnockbackBaseDuration + (KnockbackDistance * KnockbackDurationPerDistance);

	// Get NPC multiplier if applicable
	float NPCMultiplier = 1.0f;
	if (AShooterNPC* NPC = Cast<AShooterNPC>(HitActor))
	{
		NPCMultiplier = NPC->GetKnockbackDistanceMultiplier();
	}

	// Apply NPC multiplier to distance
	KnockbackDistance *= NPCMultiplier;

	// Try ShooterNPC first (has distance-based ApplyKnockback)
	if (AShooterNPC* NPC = Cast<AShooterNPC>(HitActor))
	{
		// NPC will apply its own multiplier, so divide it out
		float DistanceForNPC = KnockbackDistance / NPCMultiplier;
		NPC->ApplyKnockback(KnockbackDirection, DistanceForNPC, KnockbackDur, PlayerCenter);
		return;
	}

	// For generic characters, convert to velocity-based launch
	if (ACharacter* HitCharacter = Cast<ACharacter>(HitActor))
	{
		FVector KnockbackVelocity = KnockbackDirection * (KnockbackDistance / KnockbackDur);
		HitCharacter->LaunchCharacter(KnockbackVelocity, true, true);
		return;
	}

	// Fallback to physics impulse for non-characters
	if (UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(HitActor->GetRootComponent()))
	{
		if (RootPrimitive->IsSimulatingPhysics())
		{
			float Mass = RootPrimitive->GetMass();
			FVector Impulse = KnockbackDirection * (KnockbackDistance / KnockbackDur) * Mass;
			RootPrimitive->AddImpulse(Impulse);
		}
	}
}

// ==================== Target Magnetism ====================

void AShooterWeapon_Melee::StartMagnetism()
{
	if (!bEnableTargetMagnetism || !PawnOwner || !PawnOwner->GetController())
	{
		return;
	}

	MagnetismTarget.Reset();
	bIsDropKick = false;
	DropKickHeightDifference = 0.0f;
	bIsMagnetismActive = true;
	LungeProgress = 0.0f;

	// Check for drop kick first (airborne + looking down)
	if (ShouldPerformDropKick() && TryStartDropKick())
	{
		return;
	}

	// Get camera direction for magnetism trace
	FVector CameraLocation;
	FRotator CameraRotation;
	PawnOwner->GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);
	FVector Forward = CameraRotation.Vector();

	FVector Start = CameraLocation + Forward * TraceForwardOffset;
	FVector End = Start + Forward * MagnetismRange;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(PawnOwner);

	TArray<FHitResult> HitResults;
	bool bHit = GetWorld()->SweepMultiByChannel(
		HitResults,
		Start,
		End,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(MagnetismRadius),
		QueryParams
	);

	if (bHit)
	{
		// Find the closest valid character target
		float ClosestDist = FLT_MAX;
		AActor* ClosestTarget = nullptr;

		for (const FHitResult& Hit : HitResults)
		{
			AActor* HitActor = Hit.GetActor();
			if (HitActor && HitActor != PawnOwner && Cast<ACharacter>(HitActor))
			{
				float Dist = FVector::DistSquared(Start, Hit.ImpactPoint);
				if (Dist < ClosestDist)
				{
					ClosestDist = Dist;
					ClosestTarget = HitActor;
				}
			}
		}

		if (ClosestTarget && bEnableLunge)
		{
			// Calculate lunge target position (stop at AttackRange - Buffer from enemy)
			FVector PlayerPos = PawnOwner->GetActorLocation();
			FVector TargetPos = ClosestTarget->GetActorLocation();
			float DistanceToTarget = FVector::Dist(PlayerPos, TargetPos);

			float StopDistance = AttackRange - LungeStopBuffer;
			FVector DirectionFromTarget = (PlayerPos - TargetPos).GetSafeNormal();
			FVector IdealLungePos = TargetPos + DirectionFromTarget * StopDistance;

			// Path validation via SweepSphere
			FHitResult SweepHit;
			FCollisionQueryParams SweepParams;
			SweepParams.AddIgnoredActor(PawnOwner);
			SweepParams.AddIgnoredActor(ClosestTarget);

			bool bPathBlocked = GetWorld()->SweepSingleByChannel(
				SweepHit,
				PlayerPos,
				IdealLungePos,
				FQuat::Identity,
				ECC_Visibility,
				FCollisionShape::MakeSphere(LungeStopBuffer),
				SweepParams
			);

			if (!bPathBlocked)
			{
				MagnetismTarget = ClosestTarget;
				MagnetismLungeTargetPosition = IdealLungePos;

				// Start camera focus when lunge target is found
				StartCameraFocus(ClosestTarget);

				// Disable gravity during lock-on for smooth Z-alignment
				if (ACharacter* OwnerChar = Cast<ACharacter>(PawnOwner))
				{
					if (UCharacterMovementComponent* Movement = OwnerChar->GetCharacterMovement())
					{
						Movement->GravityScale = 0.0f;
					}
				}
			}
		}
		else if (ClosestTarget)
		{
			// No lunge - simple magnetism
			MagnetismTarget = ClosestTarget;
			StartCameraFocus(ClosestTarget);
		}
	}
}

void AShooterWeapon_Melee::UpdateMagnetism(float DeltaTime)
{
	if (!bIsMagnetismActive)
	{
		return;
	}

	// Handle drop kick movement separately
	if (bIsDropKick)
	{
		UpdateDropKick(DeltaTime);
		return;
	}

	if (!bEnableTargetMagnetism || !MagnetismTarget.IsValid() || !PawnOwner)
	{
		return;
	}

	AActor* Target = MagnetismTarget.Get();
	ACharacter* TargetChar = Cast<ACharacter>(Target);
	if (!TargetChar)
	{
		return;
	}

	// Skip magnetism if target NPC is in knockback state
	if (AShooterNPC* TargetNPC = Cast<AShooterNPC>(Target))
	{
		if (TargetNPC->IsInKnockback())
		{
			return;
		}
	}

	if (bEnableLunge)
	{
		// Dynamic Z-alignment: update lunge target Z to match target's current height
		FVector TargetPos = Target->GetActorLocation();
		FVector PlayerPos = PawnOwner->GetActorLocation();

		FVector DirectionFromTarget = (PlayerPos - TargetPos);
		DirectionFromTarget.Z = 0.0f;
		DirectionFromTarget.Normalize();

		float StopDistance = AttackRange - LungeStopBuffer;
		FVector NewLungePos = TargetPos + DirectionFromTarget * StopDistance;
		MagnetismLungeTargetPosition.Z = NewLungePos.Z;
	}
}

void AShooterWeapon_Melee::StopMagnetism()
{
	if (!bIsMagnetismActive)
	{
		return;
	}

	// Handle drop kick exit momentum before resetting state
	if (bIsDropKick && PawnOwner)
	{
		if (ACharacter* OwnerChar = Cast<ACharacter>(PawnOwner))
		{
			if (UCharacterMovementComponent* Movement = OwnerChar->GetCharacterMovement())
			{
				FVector InputVector = Movement->GetPendingInputVector();
				if (InputVector.IsNearlyZero())
				{
					InputVector = Movement->GetLastInputVector();
				}

				FVector ForwardDir = OwnerChar->GetActorForwardVector();
				ForwardDir.Z = 0.0f;
				ForwardDir.Normalize();

				float ForwardInput = FVector::DotProduct(InputVector, ForwardDir);

				if (ForwardInput > 0.1f && !DropKickVelocity.IsNearlyZero())
				{
					float ExitSpeed = DropKickVelocity.Size() * 0.5f;
					Movement->Velocity = ForwardDir * ExitSpeed;
				}
				else
				{
					Movement->Velocity = FVector::ZeroVector;
				}
			}
		}
	}

	MagnetismTarget.Reset();
	bIsMagnetismActive = false;

	// Reset drop kick state
	bIsDropKick = false;
	DropKickHeightDifference = 0.0f;
	DropKickTargetPosition = FVector::ZeroVector;
	DropKickVelocity = FVector::ZeroVector;

	// Restore gravity after lock-on
	if (PawnOwner)
	{
		if (ACharacter* OwnerChar = Cast<ACharacter>(PawnOwner))
		{
			if (UCharacterMovementComponent* Movement = OwnerChar->GetCharacterMovement())
			{
				Movement->GravityScale = 1.0f;
			}
		}
	}
}

// ==================== Momentum ====================

void AShooterWeapon_Melee::UpdateMomentumPreservation(float DeltaTime)
{
	if (!PawnOwner)
	{
		return;
	}

	ACharacter* OwnerChar = Cast<ACharacter>(PawnOwner);
	if (!OwnerChar)
	{
		return;
	}

	UCharacterMovementComponent* Movement = OwnerChar->GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// Start with preserved velocity from attack start
	FVector PreservedVelocity = VelocityAtSwingStart * MomentumPreservationRatio;
	PreservedVelocity.Z = Movement->Velocity.Z; // Keep current Z (gravity applied)

	// If we have a magnetism target and lunge is enabled, move toward them
	if (bEnableLunge && MagnetismTarget.IsValid() && !bIsDropKick)
	{
		FVector PlayerPos = OwnerChar->GetActorLocation();
		FVector ToLungeTarget = MagnetismLungeTargetPosition - PlayerPos;
		float DistToLungeTarget = ToLungeTarget.Size();

		float CurrentSpeed = VelocityAtSwingStart.Size();
		if (CurrentSpeed >= MinSpeedForLunge && DistToLungeTarget > 10.0f)
		{
			float LungeAlpha = FMath::Clamp(LungeProgress, 0.0f, 1.0f);

			float TimeRemaining = LungeDuration * (1.0f - LungeAlpha);
			if (TimeRemaining > 0.01f)
			{
				PreservedVelocity = ToLungeTarget / TimeRemaining;

				// Clamp to prevent excessive speeds
				float MaxSpeed = 3000.0f;
				if (PreservedVelocity.Size() > MaxSpeed)
				{
					PreservedVelocity = PreservedVelocity.GetSafeNormal() * MaxSpeed;
				}
			}
			else
			{
				PreservedVelocity = FVector::ZeroVector;
			}
		}
	}

	Movement->Velocity = PreservedVelocity;

	// Update lunge progress
	if (LungeDuration > 0.0f)
	{
		LungeProgress += DeltaTime / LungeDuration;
		LungeProgress = FMath::Clamp(LungeProgress, 0.0f, 1.0f);
	}
}

// ==================== Cool Kick ====================

void AShooterWeapon_Melee::StartCoolKick()
{
	if (CoolKickDuration <= 0.0f || CoolKickSpeedBoost <= 0.0f || !PawnOwner)
	{
		return;
	}

	CoolKickTimeRemaining = CoolKickDuration;

	// Use current movement direction for boost
	if (ACharacter* OwnerChar = Cast<ACharacter>(PawnOwner))
	{
		if (UCharacterMovementComponent* Movement = OwnerChar->GetCharacterMovement())
		{
			CoolKickDirection = Movement->Velocity;
			CoolKickDirection.Z = 0.0f;
			CoolKickDirection.Normalize();

			// Fallback to camera direction if not moving
			if (CoolKickDirection.IsNearlyZero())
			{
				FVector CameraLocation;
				FRotator CameraRotation;
				PawnOwner->GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);
				CoolKickDirection = CameraRotation.Vector();
				CoolKickDirection.Z = 0.0f;
				CoolKickDirection.Normalize();
			}
		}
	}
}

void AShooterWeapon_Melee::UpdateCoolKick(float DeltaTime)
{
	if (CoolKickTimeRemaining <= 0.0f || !PawnOwner)
	{
		return;
	}

	CoolKickTimeRemaining -= DeltaTime;

	if (ACharacter* OwnerChar = Cast<ACharacter>(PawnOwner))
	{
		if (UCharacterMovementComponent* Movement = OwnerChar->GetCharacterMovement())
		{
			// Add boost gradually over the period
			float BoostThisFrame = (CoolKickSpeedBoost / CoolKickDuration) * DeltaTime;
			FVector BoostVelocity = CoolKickDirection * BoostThisFrame;
			Movement->Velocity += BoostVelocity;
		}
	}

	if (CoolKickTimeRemaining <= 0.0f)
	{
		CoolKickTimeRemaining = 0.0f;
	}
}

// ==================== Drop Kick ====================

bool AShooterWeapon_Melee::ShouldPerformDropKick() const
{
	if (!bEnableDropKick || !PawnOwner || !PawnOwner->GetController())
	{
		return false;
	}

	// Must be airborne
	ACharacter* OwnerChar = Cast<ACharacter>(PawnOwner);
	if (!OwnerChar)
	{
		return false;
	}

	UCharacterMovementComponent* Movement = OwnerChar->GetCharacterMovement();
	if (!Movement || !Movement->IsFalling())
	{
		return false;
	}

	// Must be looking down enough
	FVector CameraLocation;
	FRotator CameraRotation;
	PawnOwner->GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);

	if (CameraRotation.Pitch > -DropKickPitchThreshold)
	{
		return false;
	}

	return true;
}

bool AShooterWeapon_Melee::TryStartDropKick()
{
	if (!PawnOwner || !PawnOwner->GetController())
	{
		return false;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	PawnOwner->GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);
	FVector CameraForward = CameraRotation.Vector();

	FVector TraceStart = CameraLocation;
	FVector TraceEnd = TraceStart + CameraForward * DropKickMaxRange;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(PawnOwner);

	TArray<FHitResult> HitResults;
	bool bHit = GetWorld()->SweepMultiByChannel(
		HitResults,
		TraceStart,
		TraceEnd,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(AttackRadius),
		QueryParams
	);

	if (!bHit)
	{
		return false;
	}

	AActor* BestTarget = nullptr;
	float BestDist = FLT_MAX;

	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitActor == PawnOwner || !Cast<ACharacter>(HitActor))
		{
			continue;
		}

		// Check cone angle
		FVector ToTarget = (HitActor->GetActorLocation() - TraceStart).GetSafeNormal();
		float Angle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(CameraForward, ToTarget)));
		if (Angle > DropKickConeAngle)
		{
			continue;
		}

		// Check height difference
		float HeightDiff = PawnOwner->GetActorLocation().Z - HitActor->GetActorLocation().Z;
		if (HeightDiff < DropKickMinHeightDifference)
		{
			continue;
		}

		float Dist = FVector::Dist(TraceStart, HitActor->GetActorLocation());
		if (Dist < BestDist)
		{
			BestDist = Dist;
			BestTarget = HitActor;
		}
	}

	if (!BestTarget)
	{
		return false;
	}

	// Start drop kick
	bIsDropKick = true;
	MagnetismTarget = BestTarget;
	DropKickTargetPosition = BestTarget->GetActorLocation();
	DropKickHeightDifference = PawnOwner->GetActorLocation().Z - BestTarget->GetActorLocation().Z;

	// Disable gravity during dive
	if (ACharacter* OwnerChar = Cast<ACharacter>(PawnOwner))
	{
		if (UCharacterMovementComponent* Movement = OwnerChar->GetCharacterMovement())
		{
			Movement->GravityScale = 0.0f;
		}
	}

	StartCameraFocus(BestTarget);

	return true;
}

void AShooterWeapon_Melee::UpdateDropKick(float DeltaTime)
{
	if (!bIsDropKick || !PawnOwner || !MagnetismTarget.IsValid())
	{
		return;
	}

	ACharacter* OwnerChar = Cast<ACharacter>(PawnOwner);
	if (!OwnerChar)
	{
		return;
	}

	UCharacterMovementComponent* Movement = OwnerChar->GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// Update target position
	DropKickTargetPosition = MagnetismTarget->GetActorLocation();

	// Calculate dive direction
	FVector PlayerPos = OwnerChar->GetActorLocation();
	FVector ToTarget = DropKickTargetPosition - PlayerPos;
	float DistToTarget = ToTarget.Size();

	if (DistToTarget > 10.0f)
	{
		FVector DiveDirection = ToTarget.GetSafeNormal();
		FVector DiveVelocity = DiveDirection * DropKickDiveSpeed;

		Movement->Velocity = DiveVelocity;
		DropKickVelocity = DiveVelocity;
	}
}

bool AShooterWeapon_Melee::HasDropKickTarget() const
{
	if (!bEnableDropKick || !PawnOwner || !PawnOwner->GetController())
	{
		return false;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	PawnOwner->GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);
	FVector CameraForward = CameraRotation.Vector();

	FVector TraceStart = CameraLocation;
	FVector TraceEnd = TraceStart + CameraForward * DropKickMaxRange;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(PawnOwner);

	TArray<FHitResult> HitResults;
	bool bHit = GetWorld()->SweepMultiByChannel(
		HitResults,
		TraceStart,
		TraceEnd,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(AttackRadius),
		QueryParams
	);

	if (!bHit)
	{
		return false;
	}

	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitActor == PawnOwner || !Cast<ACharacter>(HitActor))
		{
			continue;
		}

		FVector ToTarget = (HitActor->GetActorLocation() - TraceStart).GetSafeNormal();
		float Angle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(CameraForward, ToTarget)));
		if (Angle > DropKickConeAngle)
		{
			continue;
		}

		float HeightDiff = PawnOwner->GetActorLocation().Z - HitActor->GetActorLocation().Z;
		if (HeightDiff < DropKickMinHeightDifference)
		{
			continue;
		}

		return true;
	}

	return false;
}

// ==================== Camera Focus ====================

void AShooterWeapon_Melee::StartCameraFocus(AActor* Target)
{
	if (!bEnableCameraFocusOnLunge || !Target || !CachedPlayerController)
	{
		return;
	}

	CameraFocusTarget = Target;
	CameraFocusTimeRemaining = CameraFocusDuration;
	CameraFocusStartRotation = CachedPlayerController->GetControlRotation();

	FVector ToTarget = Target->GetActorLocation() - PawnOwner->GetActorLocation();
	CameraFocusTargetRotation = ToTarget.Rotation();

	// Only adjust yaw, keep pitch from player
	CameraFocusTargetRotation.Pitch = CameraFocusStartRotation.Pitch;
	CameraFocusTargetRotation.Roll = CameraFocusStartRotation.Roll;
}

void AShooterWeapon_Melee::UpdateCameraFocus(float DeltaTime)
{
	if (CameraFocusTimeRemaining <= 0.0f || !CachedPlayerController || !CameraFocusTarget.IsValid())
	{
		return;
	}

	CameraFocusTimeRemaining -= DeltaTime;

	// Calculate focus alpha
	float Alpha = 1.0f - (CameraFocusTimeRemaining / CameraFocusDuration);
	Alpha = FMath::Clamp(Alpha * CameraFocusStrength, 0.0f, 1.0f);

	// Update target rotation based on current target position
	FVector ToTarget = CameraFocusTarget->GetActorLocation() - PawnOwner->GetActorLocation();
	CameraFocusTargetRotation = ToTarget.Rotation();
	CameraFocusTargetRotation.Pitch = CachedPlayerController->GetControlRotation().Pitch;
	CameraFocusTargetRotation.Roll = 0.0f;

	// Interpolate
	FRotator NewRotation = FMath::RInterpTo(
		CachedPlayerController->GetControlRotation(),
		CameraFocusTargetRotation,
		DeltaTime,
		CameraFocusStrength * 60.0f
	);

	// Only adjust yaw
	FRotator CurrentRotation = CachedPlayerController->GetControlRotation();
	NewRotation.Pitch = CurrentRotation.Pitch;
	NewRotation.Roll = CurrentRotation.Roll;

	CachedPlayerController->SetControlRotation(NewRotation);

	if (CameraFocusTimeRemaining <= 0.0f)
	{
		CameraFocusTimeRemaining = 0.0f;
	}
}

void AShooterWeapon_Melee::StopCameraFocus()
{
	CameraFocusTarget.Reset();
	CameraFocusTimeRemaining = 0.0f;
}

// ==================== Animation ====================

const FMeleeWeaponSwingData* AShooterWeapon_Melee::SelectWeightedSwing()
{
	if (SwingAnimations.Num() == 0)
	{
		return nullptr;
	}

	if (SwingAnimations.Num() == 1)
	{
		return &SwingAnimations[0];
	}

	// Calculate total weight
	float TotalWeight = 0.0f;
	for (const FMeleeWeaponSwingData& Swing : SwingAnimations)
	{
		TotalWeight += Swing.Weight;
	}

	if (TotalWeight <= 0.0f)
	{
		return &SwingAnimations[0];
	}

	// Select random, avoiding same animation twice in a row
	int32 SelectedIndex = -1;
	int32 Attempts = 0;
	while (SelectedIndex == -1 || (SelectedIndex == LastSwingIndex && Attempts < 3))
	{
		float RandomValue = FMath::FRandRange(0.0f, TotalWeight);
		float Accumulator = 0.0f;

		for (int32 i = 0; i < SwingAnimations.Num(); ++i)
		{
			Accumulator += SwingAnimations[i].Weight;
			if (RandomValue <= Accumulator)
			{
				SelectedIndex = i;
				break;
			}
		}
		Attempts++;
	}

	if (SelectedIndex >= 0)
	{
		LastSwingIndex = SelectedIndex;
		return &SwingAnimations[SelectedIndex];
	}

	return &SwingAnimations[0];
}

// ==================== Montage Control ====================

void AShooterWeapon_Melee::StopCurrentMontage()
{
	if (!PawnOwner)
	{
		return;
	}

	if (ACharacter* OwnerChar = Cast<ACharacter>(PawnOwner))
	{
		if (USkeletalMeshComponent* TPMesh = OwnerChar->GetMesh())
		{
			if (UAnimInstance* AnimInstance = TPMesh->GetAnimInstance())
			{
				if (AnimInstance->IsAnyMontagePlaying())
				{
					AnimInstance->Montage_Stop(0.15f);
				}
			}
		}
	}
}

// ==================== VFX/SFX ====================

void AShooterWeapon_Melee::SpawnSwingTrail()
{
	StopSwingTrail();

	if (!SwingTrailFX)
	{
		return;
	}

	USkeletalMeshComponent* WeaponMesh = GetFirstPersonMesh();
	if (!WeaponMesh)
	{
		return;
	}

	ActiveTrailFX = UNiagaraFunctionLibrary::SpawnSystemAttached(
		SwingTrailFX,
		WeaponMesh,
		TrailSocketName,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget,
		true
	);
}

void AShooterWeapon_Melee::StopSwingTrail()
{
	if (ActiveTrailFX)
	{
		ActiveTrailFX->DestroyComponent();
		ActiveTrailFX = nullptr;
	}
}

void AShooterWeapon_Melee::SpawnMeleeImpactFX(const FVector& Location, const FVector& Normal)
{
	if (!MeleeImpactFX)
	{
		return;
	}

	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		MeleeImpactFX,
		Location,
		Normal.Rotation(),
		FVector(ImpactFXScale),
		true,
		true,
		ENCPoolMethod::AutoRelease
	);
}

void AShooterWeapon_Melee::PlayMeleeSound(USoundBase* Sound)
{
	if (!Sound || !PawnOwner)
	{
		return;
	}

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		Sound,
		PawnOwner->GetActorLocation(),
		FRotator::ZeroRotator,
		1.0f,
		FMath::FRandRange(0.95f, 1.05f)
	);
}

void AShooterWeapon_Melee::PlayMeleeCameraShake(TSubclassOf<UCameraShakeBase> ShakeClass, float Scale)
{
	if (!ShakeClass || !CachedPlayerController)
	{
		if (!CachedPlayerController && PawnOwner)
		{
			CachedPlayerController = Cast<APlayerController>(PawnOwner->GetController());
		}
		if (!ShakeClass || !CachedPlayerController)
		{
			return;
		}
	}

	CachedPlayerController->ClientStartCameraShake(ShakeClass, Scale);
}
