// ShooterWeapon_Melee.cpp

#include "ShooterWeapon_Melee.h"
#include "ShooterWeaponHolder.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "Variant_Shooter/MeleeAttackComponent.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/AI/Boss/BossCharacter.h"
#include "Variant_Shooter/DamageTypes/DamageType_MomentumBonus.h"
#include "Variant_Shooter/DamageTypes/DamageType_Dropkick.h"
#include "EMFVelocityModifier.h"
#include "ShooterDummyInterface.h"
#include "PolarityCharacter.h"
#include "ApexMovementComponent.h"
#include "MovementSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Engine/DamageEvents.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Field/FieldSystemObjects.h"
#include "Variant_Shooter/AnimNotify_MeleeWindow.h"
#include "Animation/AnimMontage.h"

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

	// Sync bullet counter with durability if this weapon breaks after N hits
	if (HasLimitedDurability())
	{
		MagazineSize = MaxHitCount;
		CurrentBullets = RemainingHits;
	}
	else
	{
		CurrentBullets = MagazineSize;
	}

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

	// Nothing here drives the lunge any more. It belongs to UMeleeAttackComponent, whose own tick
	// publishes the flight into the movement simulation -- which is the whole reason for the move:
	// velocity written from this actor's Tick is not part of the move the server replays.

	// Cool kick boost (independent of damage window)
	UpdateCoolKick(DeltaTime);

	// Camera focus
	UpdateCameraFocus(DeltaTime);
}

bool AShooterWeapon_Melee::OnSecondaryAction()
{
	// ShooterCharacter asks weapon-specific upgrades before this. Melee still blocks ADS.
	return true;
}

void AShooterWeapon_Melee::OnSecondaryActionReleased()
{
	Super::OnSecondaryActionReleased();
}

// ==================== Fire ====================

void AShooterWeapon_Melee::Fire()
{
	UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] === WeaponMelee::Fire CALLED === bIsFiring=%d, bIsDropKick=%d"), bIsFiring, bIsDropKick);

	if (!bIsFiring)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] Fire: BLOCKED - bIsFiring=false"));
		return;
	}

	// Don't interrupt an active drop kick with refire.
	// bIsDropKick stays true after OnDelegatedDropKickEnded to suppress the montage's
	// damage window notify. Clear it here once MeleeAttackComponent is no longer attacking.
	if (bIsDropKick)
	{
		AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(PawnOwner);
		UMeleeAttackComponent* MeleeComp = ShooterChar ? ShooterChar->GetMeleeAttackComponent() : nullptr;
		if (MeleeComp && MeleeComp->IsAttacking())
		{
			UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] Fire: BLOCKED - previous dropkick still in progress"));
			return; // Dropkick still in progress
		}
		// Dropkick ended, safe to clear and proceed with new attack
		bIsDropKick = false;
	}

	// Reset combo if too much time passed since last swing
	const float TimeSinceLastSwing = GetWorld()->GetTimeSeconds() - TimeOfLastShot;
	if (TimeSinceLastSwing > RefireRate * 3.0f)
	{
		bIsInCombo = false;
	}

	// Keep ammo full (only for infinite-durability weapons; limited ones use RemainingHits)
	if (!HasLimitedDurability())
	{
		CurrentBullets = MagazineSize;
	}

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

	// Stop the previous lunge, wherever it is in its flight. Refire is faster than a lunge lasts.
	if (UMeleeAttackComponent* MeleeComp = GetOwnerMeleeComponent())
	{
		MeleeComp->EndDelegatedLunge();
	}

	// Reset state for new swing
	bHitDuringWindow = false;
	HitActorsThisSwing.Empty();

	// Stop current montage so the new one can play
	StopCurrentMontage();

	// ==================== Delegated Drop Kick ====================
	// Delegate dropkick movement/hit-detection to MeleeAttackComponent (uses its proven code path)
	// ShooterWeapon_Melee handles its own animation and damage via delegates
	if (ShouldPerformDropKick())
	{
		AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(PawnOwner);
		UMeleeAttackComponent* MeleeComp = ShooterChar ? ShooterChar->GetMeleeAttackComponent() : nullptr;

		if (MeleeComp && MeleeComp->StartDelegatedDropKick())
		{
			bIsDropKick = true;
			DropKickHeightDifference = MeleeComp->GetDropKickHeightDifference();

			// Bind to MeleeAttackComponent delegates
			MeleeComp->OnDropKickHit.AddDynamic(this, &AShooterWeapon_Melee::OnDelegatedDropKickHit);
			MeleeComp->OnMeleeAttackEnded.AddDynamic(this, &AShooterWeapon_Melee::OnDelegatedDropKickEnded);

			// Play our own air attack animation on the weapon mesh
			CurrentSwingData = SelectWeightedSwing(/*bAirborne=*/ true);
			if (CurrentSwingData && CurrentSwingData->SwingMontage)
			{
				PlaySwingMontages(CurrentSwingData->SwingMontage, CurrentSwingData->SwingMontageTP,
					CurrentSwingData->BasePlayRate);
				PlayMeleeCameraShake(CurrentSwingData->SwingCameraShake, CurrentSwingData->SwingShakeScale);
			}

			SpawnSwingTrail();
			PlayMeleeSound(SwingSound);
			OnShotFired.Broadcast();
			TimeOfLastShot = GetWorld()->GetTimeSeconds();
			return;
		}
	}

	// ==================== Normal Attack (non-dropkick) ====================

	// Pre-attack target lock-on, borrowed from the character's melee component the same way the
	// dropkick above is. This weapon no longer owns a lunge: one implementation, one set of numbers
	// on BP_MeleeCharacter, and a flight that happens inside the movement simulation so it works for
	// a client and not only for the host.
	if (UMeleeAttackComponent* MeleeComp = GetOwnerMeleeComponent())
	{
		MeleeComp->TryStartDelegatedLunge();
	}

	// Determine if airborne for animation selection
	bool bAirborne = false;
	if (PawnOwner)
	{
		if (UCharacterMovementComponent* Movement = PawnOwner->FindComponentByClass<UCharacterMovementComponent>())
		{
			bAirborne = Movement->IsFalling();
		}
	}

	// Select and play swing animation
	CurrentSwingData = SelectWeightedSwing(bAirborne);
	if (CurrentSwingData && CurrentSwingData->SwingMontage)
	{
		PlaySwingMontages(CurrentSwingData->SwingMontage, CurrentSwingData->SwingMontageTP,
			CurrentSwingData->BasePlayRate);
		PlayMeleeCameraShake(CurrentSwingData->SwingCameraShake, CurrentSwingData->SwingShakeScale);
	}
	else if (FiringMontage)
	{
		PlaySwingMontages(FiringMontage, nullptr);
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
	// Drop kick manages its own damage window — ignore AnimNotify
	if (bIsDropKick)
	{
		return;
	}

	// The swing now runs as two montages on the same actor (arms and body), and the notify looks
	// the weapon up through the mesh's owner, so a notify authored into both would open the window
	// twice and wipe the already-hit list mid-swing, letting one swing damage a target twice.
	// Damage notifies belong in the first-person montage only; this guard makes a stray one in the
	// third-person asset harmless instead of a double hit.
	if (bDamageWindowActive)
	{
		return;
	}

	bDamageWindowActive = true;
	bHitDuringWindow = false;
	HitActorsThisSwing.Empty();
}

void AShooterWeapon_Melee::DeactivateDamageWindow()
{
	// Drop kick manages its own damage window — ignore AnimNotify
	if (bIsDropKick)
	{
		return;
	}

	bDamageWindowActive = false;

	// Play miss sound if nothing was hit during the window
	if (!bHitDuringWindow)
	{
		PlayMeleeSound(MissSound);

		// Titanfall 2: Restore pre-attack velocity on miss — only if it was faster
		// (don't kill current velocity if player accelerated after swing start)
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

				// Only restore if saved velocity was faster than current (don't slow the player down)
				if (RestoredVelocity.SizeSquared() > Movement->Velocity.SizeSquared())
				{
					Movement->Velocity = RestoredVelocity;
				}
			}
		}
	}

	// Consume one durability hit per swing (not per enemy hit)
	// Reset bHitDuringWindow BEFORE decrementing so repeated calls
	// (from multiple AnimNotify sources) don't consume extra durability
	if (bHitDuringWindow)
	{
		bHitDuringWindow = false;
		DecrementHitCount();
	}

	// Stop trail VFX
	StopSwingTrail();

	// End the lunge with the damage window. Gravity and move-collision come back on the flight's
	// falling edge inside the simulated move, not here.
	if (UMeleeAttackComponent* MeleeComp = GetOwnerMeleeComponent())
	{
		MeleeComp->EndDelegatedLunge();
	}

	// Stop camera focus
	StopCameraFocus();
}

// ==================== Damage Window Update ====================

void AShooterWeapon_Melee::UpdateDamageWindow()
{
	// Normal hit detection (dropkick hit detection is handled by MeleeAttackComponent)
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

	// Cool kick: if airborne and this is first hit without magnetism lunge.
	// "Without a lunge" is now asked of the component that owns the lunge, rather than of a copy of
	// the answer kept here.
	const UMeleeAttackComponent* LungeOwner = GetOwnerMeleeComponent();
	const bool bLungedAtSomething = LungeOwner && LungeOwner->GetLungeTargetActor() != nullptr;
	if (HitActorsThisSwing.Num() == 1 && !bLungedAtSomething)
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
			WeaponOwner->OnWeaponHit(HitResult.ImpactPoint, HitDirection, MeleeDamage, bHeadshot, false, HitActor);
			return;
		}
	}

	// Ionize BEFORE damage, and unconditionally. On a finisher blade the damage below is withheld
	// until the shield is down, and the charge is the only thing that moves it toward being down --
	// doing it the other way round would make such a weapon unable to ever open its own target.
	//
	// HitResult.GetComponent() carries the riot-shield rule: a hit on the body while the shield is up
	// transfers nothing, and the player has to hit the shield itself.
	if (bUseMeleeIonization)
	{
		ApplyIonizationToTarget(HitActor, HitResult.GetComponent(), MeleeIonizationChargePerHit);

		// The other half of the transfer: what the swing puts back into the person holding it.
		if (!FMath::IsNearlyZero(MeleeChargeToAttackerPerHit))
		{
			if (UEMFVelocityModifier* AttackerEMF = PawnOwner->FindComponentByClass<UEMFVelocityModifier>())
			{
				AttackerEMF->AddBonusCharge(MeleeChargeToAttackerPerHit);
			}
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
	WeaponOwner->OnWeaponHit(HitResult.ImpactPoint, HitDirection, FinalDamage, bHeadshot, bKilled, HitActor);

	// Broadcast for subscribers (Combo upgrade etc.)
	OnMeleeWeaponHit.Broadcast(HitActor, HitResult.ImpactPoint, bHeadshot, FinalDamage);
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

bool AShooterWeapon_Melee::AttackerOverridesLegacyMeleeCharge(const AActor* Attacker)
{
	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(Attacker);
	if (!Shooter)
	{
		return false;
	}

	const AShooterWeapon_Melee* Blade = Cast<AShooterWeapon_Melee>(Shooter->GetCurrentWeapon());
	return Blade && Blade->bUseMeleeIonization;
}

float AShooterWeapon_Melee::ApplyMeleeDamage(AActor* HitActor, const FHitResult& HitResult)
{
	if (!HitActor || !PawnOwner)
	{
		return 0.0f;
	}

	// A finisher blade takes nothing off a target whose shield is still up. Checked once, for the
	// whole swing, rather than per damage component: the momentum and dropkick bonuses are parts of
	// the same hit, and letting them through while the base damage is withheld would mean a target
	// that cannot be hurt still visibly loses health when the player arrives fast enough.
	//
	// Returns zero rather than skipping the hit: ProcessHit still plays the sound, the shake, the
	// impact FX and the knockback, and the ionization above still ran. The swing lands, it just does
	// not wound yet.
	if (ShouldWithholdDamageForShield(HitActor))
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

	// Apply upgrade-driven melee multiplier (e.g. Backstab: 3x on stunned NPC from behind).
	if (AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(PawnOwner))
	{
		if (UUpgradeManagerComponent* UpgradeMgr = ShooterChar->GetUpgradeManager())
		{
			const float UpgradeMult = UpgradeMgr->GetCombinedMeleeDamageMultiplier(HitActor);
			if (!FMath::IsNearlyEqual(UpgradeMult, 1.0f))
			{
				UE_LOG(LogTemp, Warning, TEXT("[MELEE_DMG_DEBUG] Sword: upgrade multiplier %.2fx vs %s — base %.1f -> %.1f"),
					UpgradeMult, *HitActor->GetName(), BaseDamage, BaseDamage * UpgradeMult);
				BaseDamage *= UpgradeMult;
			}
		}
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

UMeleeAttackComponent* AShooterWeapon_Melee::GetOwnerMeleeComponent() const
{
	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(PawnOwner);
	return Shooter ? Shooter->GetMeleeAttackComponent() : nullptr;
}

// ==================== Momentum ====================

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
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] Weapon::ShouldDropKick: FALSE - bEnableDropKick=%d, PawnOwner=%d"), bEnableDropKick, PawnOwner != nullptr);
		return false;
	}

	// Must be airborne
	ACharacter* OwnerChar = Cast<ACharacter>(PawnOwner);
	if (!OwnerChar)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] Weapon::ShouldDropKick: FALSE - no OwnerChar"));
		return false;
	}

	UCharacterMovementComponent* Movement = OwnerChar->GetCharacterMovement();
	if (!Movement || !Movement->IsFalling())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] Weapon::ShouldDropKick: FALSE - not falling (Mode=%d)"),
			Movement ? (int32)Movement->MovementMode : -1);
		return false;
	}

	// Must be looking down enough
	FVector CameraLocation;
	FRotator CameraRotation;
	PawnOwner->GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);

	if (CameraRotation.Pitch > -DropKickPitchThreshold)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] Weapon::ShouldDropKick: FALSE - Pitch=%.1f, need < -%.1f (not looking down enough)"),
			CameraRotation.Pitch, DropKickPitchThreshold);
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] Weapon::ShouldDropKick: TRUE (Pitch=%.1f, Threshold=-%.1f)"),
		CameraRotation.Pitch, DropKickPitchThreshold);
	return true;
}

void AShooterWeapon_Melee::OnDelegatedDropKickHit(AActor* HitActor, const FVector& HitLocation, float Damage)
{
	if (!HitActor || !PawnOwner)
	{
		return;
	}

	// Build a fake hit result for ProcessHit
	FHitResult FakeHit;
	FakeHit.HitObjectHandle = FActorInstanceHandle(HitActor);
	FakeHit.ImpactPoint = HitLocation;
	FakeHit.ImpactNormal = (PawnOwner->GetActorLocation() - HitLocation).GetSafeNormal();
	FakeHit.Location = HitLocation;
	FakeHit.bBlockingHit = true;

	if (UPrimitiveComponent* TargetRoot = Cast<UPrimitiveComponent>(HitActor->GetRootComponent()))
	{
		FakeHit.Component = TargetRoot;
	}

	// Use our own ProcessHit to apply weapon-specific damage, knockback, effects
	ProcessHit(FakeHit);
}

void AShooterWeapon_Melee::OnDelegatedDropKickEnded()
{
	// Unbind from MeleeAttackComponent delegates
	AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(PawnOwner);
	UMeleeAttackComponent* MeleeComp = ShooterChar ? ShooterChar->GetMeleeAttackComponent() : nullptr;
	if (MeleeComp)
	{
		MeleeComp->OnDropKickHit.RemoveDynamic(this, &AShooterWeapon_Melee::OnDelegatedDropKickHit);
		MeleeComp->OnMeleeAttackEnded.RemoveDynamic(this, &AShooterWeapon_Melee::OnDelegatedDropKickEnded);
	}

	// Play miss sound if nothing was hit
	if (!bHitDuringWindow)
	{
		PlayMeleeSound(MissSound);
	}

	// Keep bIsDropKick TRUE here — the swing montage is still playing and its
	// damage window notify would fire after this callback. Since ActivateDamageWindow()
	// checks bIsDropKick, keeping it true prevents the notify from opening a damage
	// window that would score a second hit.
	// bIsDropKick is cleared at the start of the next Fire() call, which also calls
	// StopCurrentMontage() to kill the old animation before starting a new one.
	DropKickHeightDifference = 0.0f;

	StopSwingTrail();
	StopCameraFocus();
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

float AShooterWeapon_Melee::FindDamageWindowStartTime(const UAnimMontage* Montage)
{
	if (!Montage)
	{
		return -1.0f;
	}

	for (const FAnimNotifyEvent& NotifyEvent : Montage->Notifies)
	{
		if (NotifyEvent.NotifyStateClass &&
			NotifyEvent.NotifyStateClass->IsA<UAnimNotifyState_MeleeDamageWindow>())
		{
			return NotifyEvent.GetTriggerTime();
		}

		if (NotifyEvent.Notify &&
			NotifyEvent.Notify->IsA<UAnimNotify_MeleeActivate>())
		{
			return NotifyEvent.GetTriggerTime();
		}
	}

	return -1.0f;
}

const FMeleeWeaponSwingData* AShooterWeapon_Melee::SelectWeightedSwing(bool bAirborne)
{
	// Choose animation pool: air or ground. Fallback to ground if air pool is empty.
	TArray<FMeleeWeaponSwingData>& AnimPool = (bAirborne && AirSwingAnimations.Num() > 0) ? AirSwingAnimations : SwingAnimations;

	if (AnimPool.Num() == 0)
	{
		return nullptr;
	}

	// Determine which side we need for this swing
	EMeleeSwingSide NeededSide = bIsInCombo ? CurrentSwingSide : FirstSwingSide;

	// Collect indices matching the needed side
	TArray<int32> MatchingIndices;
	float TotalWeight = 0.0f;
	for (int32 i = 0; i < AnimPool.Num(); ++i)
	{
		if (AnimPool[i].SwingSide == NeededSide)
		{
			MatchingIndices.Add(i);
			TotalWeight += AnimPool[i].Weight;
		}
	}

	// Fallback: if no animations for this side, use all animations from pool
	if (MatchingIndices.Num() == 0)
	{
		for (int32 i = 0; i < AnimPool.Num(); ++i)
		{
			MatchingIndices.Add(i);
			TotalWeight += AnimPool[i].Weight;
		}
	}

	if (TotalWeight <= 0.0f || MatchingIndices.Num() == 0)
	{
		return &AnimPool[0];
	}

	// Select random from matching side, avoiding same animation twice in a row
	int32 SelectedIndex = -1;
	int32 Attempts = 0;
	while (SelectedIndex == -1 || (SelectedIndex == LastSwingIndex && MatchingIndices.Num() > 1 && Attempts < 3))
	{
		float RandomValue = FMath::FRandRange(0.0f, TotalWeight);
		float Accumulator = 0.0f;

		for (int32 Idx : MatchingIndices)
		{
			Accumulator += AnimPool[Idx].Weight;
			if (RandomValue <= Accumulator)
			{
				SelectedIndex = Idx;
				break;
			}
		}
		Attempts++;
	}

	if (SelectedIndex >= 0)
	{
		LastSwingIndex = SelectedIndex;

		// Alternate side for next swing
		CurrentSwingSide = (NeededSide == EMeleeSwingSide::Left) ? EMeleeSwingSide::Right : EMeleeSwingSide::Left;
		bIsInCombo = true;

		return &AnimPool[SelectedIndex];
	}

	return &AnimPool[0];
}

// ==================== Montage Control ====================

void AShooterWeapon_Melee::StopCurrentMontage()
{
	if (!PawnOwner)
	{
		return;
	}

	// Stop on TP mesh
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

	// Stop on the owner's first-person arms
	if (USkeletalMeshComponent* FPMesh = GetOwnerFirstPersonMesh())
	{
		if (UAnimInstance* AnimInstance = FPMesh->GetAnimInstance())
		{
			if (AnimInstance->IsAnyMontagePlaying())
			{
				AnimInstance->Montage_Stop(0.15f);
			}
		}
	}
}

USkeletalMeshComponent* AShooterWeapon_Melee::GetOwnerFirstPersonMesh() const
{
	if (AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(PawnOwner))
	{
		return ShooterChar->GetFirstPersonMesh();
	}
	return nullptr;
}

void AShooterWeapon_Melee::PlaySwingMontages(UAnimMontage* FirstPersonMontage, UAnimMontage* ThirdPersonMontage, float PlayRate)
{
	AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(PawnOwner);
	if (!ShooterChar)
	{
		return;
	}

	const float FinalRate = PlayRate * ComboSpeedMultiplier;

	// The arms exist on this machine only, so they are played straight.
	if (FirstPersonMontage)
	{
		if (USkeletalMeshComponent* FPMesh = ShooterChar->GetFirstPersonMesh())
		{
			if (UAnimInstance* AnimInstance = FPMesh->GetAnimInstance())
			{
				if (!AnimInstance->Montage_IsPlaying(FirstPersonMontage))
				{
					AnimInstance->Montage_Play(FirstPersonMontage, FinalRate);
				}
			}
		}
	}

	// The body is what teammates are looking at, so the swing goes out to every machine at the
	// same rate. Falling back to the first-person montage keeps a half-authored weapon visible
	// to the rest of the team rather than silently swinging nothing.
	UAnimMontage* BodyMontage = ThirdPersonMontage ? ThirdPersonMontage : FirstPersonMontage;
	if (BodyMontage)
	{
		ShooterChar->PlayThirdPersonMontageEverywhere(BodyMontage, FinalRate);
	}
}

// ==================== Combo Speed ====================

void AShooterWeapon_Melee::ApplyComboSpeedMultiplier(float NewMultiplier)
{
	const float Clamped = FMath::Max(0.1f, NewMultiplier);
	if (FMath::IsNearlyEqual(Clamped, ComboSpeedMultiplier))
	{
		return;
	}

	ComboSpeedMultiplier = Clamped;

	// Adjust the play rate of any currently-playing swing montage so the visual
	// reflects the new combo speed immediately rather than waiting for the next swing.
	if (USkeletalMeshComponent* FPMesh = GetOwnerFirstPersonMesh())
	{
		if (UAnimInstance* AnimInstance = FPMesh->GetAnimInstance())
		{
			if (CurrentSwingData && CurrentSwingData->SwingMontage &&
				AnimInstance->Montage_IsPlaying(CurrentSwingData->SwingMontage))
			{
				AnimInstance->Montage_SetPlayRate(CurrentSwingData->SwingMontage,
					CurrentSwingData->BasePlayRate * ComboSpeedMultiplier);
			}
		}
	}
}

float AShooterWeapon_Melee::GetCurrentRefireRate() const
{
	// Base implementation already factors in heat. Combo divides on top — clamp to a tiny
	// positive number so we never return 0 / negative.
	const float Base = Super::GetCurrentRefireRate();
	return Base / FMath::Max(0.1f, ComboSpeedMultiplier);
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

// ==================== Durability ====================

void AShooterWeapon_Melee::SetRemainingHits(int32 Hits)
{
	MaxHitCount = Hits;
	RemainingHits = Hits;
	BroadcastDurabilityUpdate();
}

void AShooterWeapon_Melee::AddRemainingHits(int32 HitsToAdd)
{
	RemainingHits += HitsToAdd;
	MaxHitCount = RemainingHits;
	BroadcastDurabilityUpdate();
}

void AShooterWeapon_Melee::SetBreakData(UGeometryCollection* GC, float Impulse, float AngularImpulse, float GibLifetime)
{
	BreakGeometryCollection = GC;
	BreakImpulse = Impulse;
	BreakAngularImpulse = AngularImpulse;
	BreakGibLifetime = GibLifetime;
}

bool AShooterWeapon_Melee::DecrementHitCount()
{
	if (!HasLimitedDurability())
	{
		return false;
	}

	--RemainingHits;
	BroadcastDurabilityUpdate();

	if (RemainingHits <= 0)
	{
		BreakWeapon();
		return true;
	}
	return false;
}

void AShooterWeapon_Melee::BroadcastDurabilityUpdate()
{
	if (!HasLimitedDurability())
	{
		return;
	}

	MagazineSize = MaxHitCount;
	CurrentBullets = RemainingHits;

	if (AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(PawnOwner))
	{
		ShooterChar->OnBulletCountUpdated.Broadcast(MaxHitCount, RemainingHits);
	}
}

void AShooterWeapon_Melee::BreakWeapon()
{
	// 1. Spawn GC shatter
	SpawnBreakDestructionGC();

	// 2. Play break sound
	PlayMeleeSound(BreakSound);

	// 3. Remove weapon from player inventory and switch.
	// Deferred to next tick: BreakWeapon can be called from a TP-mesh AnimNotify
	// (DeactivateDamageWindowFromNotify → DecrementHitCount → BreakWeapon). The swap
	// chain ends in OnWeaponActivated → SetAnimInstanceClass on the same mesh, which
	// UE asserts against while PostAnimEvaluation is in progress (recursion check).
	AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(PawnOwner);
	if (ShooterChar)
	{
		TWeakObjectPtr<AShooterCharacter> WeakChar(ShooterChar);
		TWeakObjectPtr<AShooterWeapon_Melee> WeakThis(this);
		GetWorld()->GetTimerManager().SetTimerForNextTick([WeakChar, WeakThis]()
		{
			if (WeakChar.IsValid() && WeakThis.IsValid())
			{
				WeakChar->RemoveMeleeWeapon(WeakThis.Get());
			}
		});
	}
}

void AShooterWeapon_Melee::SpawnBreakDestructionGC()
{
	if (!BreakGeometryCollection)
	{
		return;
	}

	// The gibs are world geometry that everyone can see, so they are placed on the third-person
	// mesh: the first-person one sits in front of the owner's camera and would scatter debris
	// through the middle of the screen from a position nobody else shares.
	USkeletalMeshComponent* SwordMesh = GetThirdPersonMesh();
	if (!SwordMesh || !SwordMesh->GetSkeletalMeshAsset())
	{
		SwordMesh = GetFirstPersonMesh();
	}
	if (!SwordMesh || !SwordMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	const FTransform MeshTransform = SwordMesh->GetComponentTransform();
	const FVector Origin = MeshTransform.GetLocation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AGeometryCollectionActor* GCActor = GetWorld()->SpawnActor<AGeometryCollectionActor>(
		Origin, MeshTransform.GetRotation().Rotator(), SpawnParams);

	if (!GCActor)
	{
		return;
	}

	UGeometryCollectionComponent* GCComp = GCActor->GetGeometryCollectionComponent();
	if (!GCComp)
	{
		GCActor->Destroy();
		return;
	}

	GCActor->SetActorScale3D(MeshTransform.GetScale3D());

	GCComp->SetCollisionProfileName(FName("Ragdoll"));
	GCComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	GCComp->SetRestCollection(BreakGeometryCollection);

	// Copy materials from the sword static mesh
	const int32 NumMats = SwordMesh->GetNumMaterials();
	for (int32 i = 0; i < NumMats; i++)
	{
		if (UMaterialInterface* Mat = SwordMesh->GetMaterial(i))
		{
			GCComp->SetMaterial(i, Mat);
		}
	}

	GCComp->SetSimulatePhysics(true);
	GCComp->RecreatePhysicsState();

	// Break all clusters
	UUniformScalar* StrainField = NewObject<UUniformScalar>(GCActor);
	StrainField->Magnitude = 999999.0f;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
		nullptr, StrainField);

	// Scatter pieces radially
	URadialVector* RadialVelocity = NewObject<URadialVector>(GCActor);
	RadialVelocity->Magnitude = BreakImpulse;
	RadialVelocity->Position = Origin;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity,
		nullptr, RadialVelocity);

	// Angular velocity for tumbling
	URadialVector* AngularVelocity = NewObject<URadialVector>(GCActor);
	AngularVelocity->Magnitude = BreakAngularImpulse;
	AngularVelocity->Position = Origin;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_AngularVelocity,
		nullptr, AngularVelocity);

	GCActor->SetLifeSpan(BreakGibLifetime);

	// Hide both sword meshes (GC gibs replace them) — the owner sees the first-person one, and
	// everybody else the third-person one, so leaving either up shows an unbroken sword.
	if (USkeletalMeshComponent* FPSword = GetFirstPersonMesh())
	{
		FPSword->SetVisibility(false, /*bPropagateToChildren=*/ true);
	}
	if (USkeletalMeshComponent* TPSword = GetThirdPersonMesh())
	{
		TPSword->SetVisibility(false, /*bPropagateToChildren=*/ true);
	}
}
