// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/AI/ShooterNPC.h"
#include "ShooterWeapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "ShooterGameMode.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "ShooterAIController.h"
#include "Components/StateTreeAIComponent.h"
#include "../../AI/Components/AIAccuracyComponent.h"
#include "../../AI/Components/MeleeRetreatComponent.h"
#include "../../AI/Coordination/AICombatCoordinator.h"
#include "Variant_Shooter/Weapons/DroppedMeleeWeapon.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"
#include "EMFVelocityModifier.h"
#include "EMF_FieldComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "../DamageTypes/DamageType_Melee.h"
#include "../DamageTypes/DamageType_Wallslam.h"
#include "../DamageTypes/DamageType_EMFProximity.h"
#include "../UI/DamageNumbersSubsystem.h"
#include "../UI/EMFChargeWidgetSubsystem.h"
#include "Curves/CurveFloat.h"
#include "NiagaraFunctionLibrary.h"
#include "Polarity/Checkpoint/CheckpointSubsystem.h"
#include "Boss/BossProjectile.h"
#include "../Pickups/HealthPickup.h"
#include "../Pickups/ArmorPickup.h"
#include "FlyingDrone.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Field/FieldSystemObjects.h"
#include "CameraShakeComponent.h"

AShooterNPC::AShooterNPC(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AccuracyComponent = CreateDefaultSubobject<UAIAccuracyComponent>(TEXT("AccuracyComponent"));
	MeleeRetreatComponent = CreateDefaultSubobject<UMeleeRetreatComponent>(TEXT("MeleeRetreatComponent"));

	// Create EMF components for charge-based interactions
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("FieldComponent"));
	EMFVelocityModifier = CreateDefaultSubobject<UEMFVelocityModifier>(TEXT("EMFVelocityModifier"));

	// Set NPC owner type for EM force filtering
	if (FieldComponent)
	{
		FieldComponent->SetOwnerType(EEMSourceOwnerType::NPC);
	}
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetOwnerType(EEMSourceOwnerType::NPC);
		// NPCs don't react to other NPCs' EM forces
		EMFVelocityModifier->NPCForceMultiplier = 0.0f;
	}

	// === Performance: optimize animation ticking for NPCs ===
	// Third person mesh (GetMesh()) - only tick animations when rendered
	if (USkeletalMeshComponent* TPMesh = GetMesh())
	{
		TPMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	}
}

void AShooterNPC::BeginPlay()
{
	Super::BeginPlay();

	// === Performance: disable CameraShake on NPCs — they don't need it ===
	if (UCameraShakeComponent* Shake = GetCameraShake())
	{
		Shake->SetComponentTickEnabled(false);
	}

	// === Performance: disable FirstPersonMesh on NPCs — not visible ===
	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		FPMesh->SetComponentTickEnabled(false);
		FPMesh->SetVisibility(false);
		FPMesh->SetHiddenInGame(true);
	}

	// spawn the weapon
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	Weapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);

	// Subscribe to weapon's shot fired delegate for burst counting
	if (Weapon)
	{
		Weapon->OnShotFired.AddDynamic(this, &AShooterNPC::OnWeaponShotFired);
	}

	// Register with combat coordinator
	RegisterWithCoordinator();

	// Register with checkpoint subsystem for respawn tracking
	if (UCheckpointSubsystem* CheckpointSubsystem = GetWorld()->GetSubsystem<UCheckpointSubsystem>())
	{
		CheckpointSubsystem->RegisterNPC(this);
	}

	// Bind capsule hit event for wall slam detection and NPC collision
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->OnComponentHit.AddDynamic(this, &AShooterNPC::OnCapsuleHit);

		// Ensure capsule generates hit events
		Capsule->SetNotifyRigidBodyCollision(true);

		// Make sure NPC capsules collide with each other (Block Pawn channel)
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}

	// Register with damage numbers subsystem for floating damage display
	if (UDamageNumbersSubsystem* DamageNumbersSubsystem = GetWorld()->GetSubsystem<UDamageNumbersSubsystem>())
	{
		DamageNumbersSubsystem->RegisterNPC(this);
	}

	// Register with EMF charge widget subsystem for overhead charge indicator
	if (UEMFChargeWidgetSubsystem* ChargeWidgetSubsystem = GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
	{
		ChargeWidgetSubsystem->RegisterNPC(this);
	}

	// Apply permanent explosion stun if configured
	if (bIsPermanentlyStunned)
	{
		// Use a large duration; EndKnockbackStun will block recovery anyway
		ApplyExplosionStun(999999.0f, ReverseChannelingStunMontage);
	}
}

void AShooterNPC::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Subscribe to AI controller's perception events
	if (AShooterAIController* ShooterAI = Cast<AShooterAIController>(NewController))
	{
		ShooterAI->OnEnemySpotted.AddDynamic(this, &AShooterNPC::OnEnemySpotted);
		ShooterAI->OnEnemyLost.AddDynamic(this, &AShooterNPC::OnEnemyLost);
		ShooterAI->OnTeamPerceptionReceived.AddDynamic(this, &AShooterNPC::OnTeamPerceptionReceived);
	}
}

void AShooterNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear all timers
	GetWorld()->GetTimerManager().ClearTimer(DeathTimer);
	GetWorld()->GetTimerManager().ClearTimer(KnockbackStunTimer);
	GetWorld()->GetTimerManager().ClearTimer(BurstCooldownTimer);
	GetWorld()->GetTimerManager().ClearTimer(PermissionRetryTimer);

	// Unregister from coordinator
	UnregisterFromCoordinator();

	// Unregister from damage numbers subsystem
	if (UDamageNumbersSubsystem* DamageNumbersSubsystem = GetWorld()->GetSubsystem<UDamageNumbersSubsystem>())
	{
		DamageNumbersSubsystem->UnregisterNPC(this);
	}

	// Unregister from EMF charge widget subsystem
	if (UEMFChargeWidgetSubsystem* ChargeWidgetSubsystem = GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
	{
		ChargeWidgetSubsystem->UnregisterNPC(this);
	}
}

void AShooterNPC::Tick(float DeltaTime)
{
	// Store velocity BEFORE Super::Tick processes any movement/collisions
	// This gives us the "pre-collision" velocity for impact damage calculation
	PreviousTickVelocity = GetVelocity();

	Super::Tick(DeltaTime);

	// Dead NPCs: skip all NPC-specific logic (character movement still ticks via Super)
	if (bIsDead)
	{
		return;
	}

	// Update knockback interpolation if active
	if (bIsKnockbackInterpolating)
	{
		UpdateKnockbackInterpolation(DeltaTime);
	}

	// Update launched state collision detection
	if (bIsLaunched)
	{
		UpdateLaunchedCollision();
	}

	// Update hit flash overlay
	if (bHitFlashActive)
	{
		UpdateHitFlash(DeltaTime);
	}

	// Update Charge/Polarity - get charge from EMFVelocityModifier
	float ChargeValue = 0.0f;
	if (EMFVelocityModifier)
	{
		ChargeValue = EMFVelocityModifier->GetCharge();
	}

	// Sync FieldComponent with EMFVelocityModifier charge (only when changed)
	if (FieldComponent && !FMath::IsNearlyEqual(ChargeValue, PreviousChargeValue, 0.001f))
	{
		FieldComponent->SetCharge(ChargeValue);
	}

	// Determine current polarity (0=Neutral, 1=Positive, 2=Negative)
	uint8 CurrentPolarity = 0; // Neutral
	if (ChargeValue > KINDA_SMALL_NUMBER)
	{
		CurrentPolarity = 1; // Positive
	}
	else if (ChargeValue < -KINDA_SMALL_NUMBER)
	{
		CurrentPolarity = 2; // Negative
	}

	// Broadcast charge update only when charge actually changed
	if (!FMath::IsNearlyEqual(ChargeValue, PreviousChargeValue, 0.001f))
	{
		OnChargeUpdated.Broadcast(ChargeValue, CurrentPolarity);
		PreviousChargeValue = ChargeValue;
	}

	// Check if polarity changed
	if (CurrentPolarity != PreviousPolarity)
	{
		OnPolarityChanged.Broadcast(CurrentPolarity, ChargeValue);
		PreviousPolarity = CurrentPolarity;
	}

	// Check for EMF proximity collision (only when not already in knockback)
	if (bEnableEMFProximityKnockback && !bIsInKnockback)
	{
		CheckEMFProximityCollision();
	}
}

float AShooterNPC::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// ignore if already dead
	if (bIsDead)
	{
		return 0.0f;
	}

	// Ignore friendly fire from other NPCs (except parried projectiles)
	bool bIsParriedProjectile = false;
	if (DamageCauser)
	{
		// Check if this is a parried BossProjectile - those SHOULD damage the boss
		if (ABossProjectile* BossProj = Cast<ABossProjectile>(DamageCauser))
		{
			if (BossProj->WasParried())
			{
				// Allow parried projectile damage to go through
				UE_LOG(LogTemp, Warning, TEXT("[ShooterNPC] Allowing parried BossProjectile damage: %.1f"), Damage);
				bIsParriedProjectile = true;
				// Don't return - let damage be applied
			}
			else
			{
				// Non-parried boss projectile - still ignore friendly fire
				return 0.0f;
			}
		}
		else
		{
			// Allow collision/physics damage types from NPCs (Wallslam, EMFProximity)
			// These come from NPC-NPC collisions and are NOT weapon friendly fire
			bool bIsCollisionDamage = DamageEvent.DamageTypeClass &&
				(DamageEvent.DamageTypeClass->IsChildOf(UDamageType_Wallslam::StaticClass()) ||
				 DamageEvent.DamageTypeClass->IsChildOf(UDamageType_EMFProximity::StaticClass()));

			if (!bIsCollisionDamage)
			{
				// Check if damage came from another ShooterNPC (through their weapon)
				AActor* DamageOwner = DamageCauser->GetOwner();
				if (Cast<AShooterNPC>(DamageCauser) || Cast<AShooterNPC>(DamageOwner))
				{
					return 0.0f;
				}
			}
		}

		// Also check the instigator's pawn (but allow parried projectiles)
		if (EventInstigator && !bIsParriedProjectile)
		{
			if (Cast<AShooterNPC>(EventInstigator->GetPawn()))
			{
				return 0.0f;
			}
		}
	}

	// Reduce HP
	CurrentHP -= Damage;

	// Check if damage is from melee attack and apply charge transfer
	if (DamageEvent.DamageTypeClass && DamageEvent.DamageTypeClass->IsChildOf(UDamageType_Melee::StaticClass()))
	{
		// Steal charge from attacker (opposite sign to what they gain)
		if (EMFVelocityModifier && EventInstigator)
		{
			APawn* Attacker = EventInstigator->GetPawn();
			if (Attacker)
			{
				// Try to get attacker's EMF component to determine their charge
				UEMFVelocityModifier* AttackerEMF = Attacker->FindComponentByClass<UEMFVelocityModifier>();

				// Calculate charge transfer: opposite sign to attacker's current charge
				float ChargeToAdd = ChargeChangeOnMeleeHit;
				if (AttackerEMF)
				{
					float AttackerCharge = AttackerEMF->GetCharge();
					// If attacker has positive charge, give NPC negative (and vice versa)
					ChargeToAdd = -FMath::Abs(ChargeChangeOnMeleeHit) * FMath::Sign(AttackerCharge);

					// If attacker is neutral, use default negative value
					if (FMath::Abs(AttackerCharge) < KINDA_SMALL_NUMBER)
					{
						ChargeToAdd = ChargeChangeOnMeleeHit;
					}

					UE_LOG(LogTemp, Warning, TEXT("NPC Melee Hit: Attacker charge=%.2f, ChargeToAdd=%.2f, NPC old charge=%.2f"),
						AttackerCharge, ChargeToAdd, EMFVelocityModifier->GetCharge());
				}

				// Add charge to EMFVelocityModifier using SetCharge (allows negative values)
				float OldCharge = EMFVelocityModifier->GetCharge();
				float NewCharge = OldCharge + ChargeToAdd;
				EMFVelocityModifier->SetCharge(NewCharge);

				UE_LOG(LogTemp, Warning, TEXT("NPC Melee Hit: NPC new charge=%.2f"), EMFVelocityModifier->GetCharge());
			}
		}
	}

	// Play hit reaction animation
	if (DamageCauser)
	{
		FVector DamageDirection = (GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal();
		PlayHitReaction(DamageDirection);
		StartHitFlash();

		// Retaliation: get immediate permission to shoot back
		if (bUseCoordinator)
		{
			if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(this))
			{
				Coordinator->GrantRetaliationPermission(this);
			}
		}

		// If we have a target and want to shoot, try immediately
		if (bWantsToShoot && CurrentAimTarget.IsValid())
		{
			TryStartShooting();
		}
		// If we don't have a target yet, set the damage causer as target
		else if (!CurrentAimTarget.IsValid())
		{
			// Get the pawn that caused damage (could be through weapon)
			APawn* AttackerPawn = nullptr;
			if (EventInstigator)
			{
				AttackerPawn = EventInstigator->GetPawn();
			}
			if (!AttackerPawn)
			{
				AttackerPawn = Cast<APawn>(DamageCauser);
			}

			if (AttackerPawn)
			{
				NotifyTargetAcquired(AttackerPawn);
				CurrentAimTarget = AttackerPawn;
				bWantsToShoot = true;
				bIsShooting = true;
				// Defer to next tick to prevent infinite recursion when NPCs shoot each other
				GetWorld()->GetTimerManager().SetTimerForNextTick(this, &AShooterNPC::TryStartShooting);
			}
		}
	}

	// Have we depleted HP?
	if (CurrentHP <= 0.0f)
	{
		// Store killing blow info before Die() for health pickup and death effects logic
		LastKillingDamageType = DamageEvent.DamageTypeClass;
		LastKillingDamageCauser = DamageCauser;

		// Compute killing hit direction for death effects (dismemberment directional bias, ragdoll impulse)
		if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
		{
			// Explosion: direction from explosion origin to NPC
			const FRadialDamageEvent& RadialEvent = static_cast<const FRadialDamageEvent&>(DamageEvent);
			LastKillingHitDirection = (GetActorLocation() - RadialEvent.Origin).GetSafeNormal();
		}
		else if (DamageCauser)
		{
			// Point/generic damage: direction from causer to NPC
			LastKillingHitDirection = (GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal();
		}
		else if (!KnockbackDirection.IsNearlyZero())
		{
			// Fallback: use current knockback direction
			LastKillingHitDirection = KnockbackDirection;
		}

		Die();
	}

	// Broadcast damage taken event for damage numbers display
	// Use a consistent location slightly above NPC center for reliable screen positioning
	// This ensures damage numbers always appear near the NPC regardless of where the hit landed
	FVector HitLocation = GetActorLocation() + FVector(0.0f, 0.0f, 50.0f);  // Above center

	OnDamageTaken.Broadcast(this, Damage, DamageEvent.DamageTypeClass, HitLocation, DamageCauser);

	return Damage;
}

void AShooterNPC::AttachWeaponMeshes(AShooterWeapon* WeaponToAttach)
{
	const FAttachmentTransformRules AttachmentRule(EAttachmentRule::SnapToTarget, false);

	// attach the weapon actor
	WeaponToAttach->AttachToActor(this, AttachmentRule);

	// attach the weapon meshes
	WeaponToAttach->GetFirstPersonMesh()->AttachToComponent(GetFirstPersonMesh(), AttachmentRule, FirstPersonWeaponSocket);
	WeaponToAttach->GetThirdPersonMesh()->AttachToComponent(GetMesh(), AttachmentRule, FirstPersonWeaponSocket);
}

void AShooterNPC::PlayFiringMontage(UAnimMontage* Montage)
{
	// unused
}

void AShooterNPC::AddWeaponRecoil(float Recoil)
{
	// unused
}

void AShooterNPC::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
	// unused
}

FVector AShooterNPC::GetWeaponTargetLocation()
{
	// start aiming from the camera location
	const FVector AimSource = GetFirstPersonCameraComponent()->GetComponentLocation();

	FVector AimDir, AimTarget = FVector::ZeroVector;

	// do we have a valid aim target?
	if (CurrentAimTarget.IsValid())
	{
		// target the actor location
		AimTarget = CurrentAimTarget.Get()->GetActorLocation();

		// apply a vertical offset to target head/feet
		AimTarget.Z += FMath::RandRange(MinAimOffsetZ, MaxAimOffsetZ);

		// Use AccuracyComponent for spread calculation
		if (AccuracyComponent)
		{
			AimDir = AccuracyComponent->CalculateAimDirection(AimTarget, CurrentAimTarget.Get());
		}
		else
		{
			// Fallback if component is missing
			AimDir = (AimTarget - AimSource).GetSafeNormal();
			AimDir = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(AimDir, AimVarianceHalfAngle);
		}
	}
	else
	{
		// no aim target, use forward direction with accuracy spread
		if (AccuracyComponent)
		{
			AimDir = AccuracyComponent->CalculateAimDirection(
				AimSource + GetFirstPersonCameraComponent()->GetForwardVector() * AimRange,
				nullptr
			);
		}
		else
		{
			AimDir = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(GetFirstPersonCameraComponent()->GetForwardVector(), AimVarianceHalfAngle);
		}
	}

	// calculate the unobstructed aim target location
	AimTarget = AimSource + (AimDir * AimRange);

	// run a visibility trace to see if there's obstructions
	FHitResult OutHit;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->LineTraceSingleByChannel(OutHit, AimSource, AimTarget, ECC_Visibility, QueryParams);

	// return either the impact point or the trace end
	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

void AShooterNPC::AddWeaponClass(const TSubclassOf<AShooterWeapon>& InWeaponClass)
{
	// unused
}

void AShooterNPC::OnWeaponActivated(AShooterWeapon* InWeapon)
{
	// unused
}

void AShooterNPC::OnWeaponDeactivated(AShooterWeapon* InWeapon)
{
	// unused
}

void AShooterNPC::OnSemiWeaponRefire()
{
	// Don't continue if dead or in knockback/captured state
	if (bIsDead || bIsInKnockback)
	{
		StopShooting();
		return;
	}

	// Don't continue if target is invalid
	if (!CurrentAimTarget.IsValid())
	{
		StopShooting();
		return;
	}

	// Continue firing if we're in an active burst (permission was checked at burst start)
	if (bIsShooting && !bInBurstCooldown)
	{
		// Update focus to current target position before continuing to fire
		if (AController* MyController = GetController())
		{
			if (AAIController* AIController = Cast<AAIController>(MyController))
			{
				AIController->SetFocus(CurrentAimTarget.Get());
			}
		}

		// fire the weapon (OnWeaponShotFired will be called via delegate)
		if (Weapon)
		{
			Weapon->StartFiring();
		}
	}
}

void AShooterNPC::Die()
{
	// ignore if already dead
	if (bIsDead)
	{
		return;
	}

	// raise the dead flag
	bIsDead = true;

	// Cache charge before disabling EMF (needed for weapon drops below)
	const float CachedNPCCharge = EMFVelocityModifier ? EMFVelocityModifier->GetCharge() : 0.0f;

	// Immediately disable EMF field so it stops affecting other objects
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetCharge(0.0f);
		EMFVelocityModifier->SetComponentTickEnabled(false);
	}
	if (FieldComponent)
	{
		FieldComponent->SetCharge(0.0f);
		FieldComponent->UnregisterFromRegistry();
		FieldComponent->SetComponentTickEnabled(false);
	}

	// Stop shooting immediately
	StopShooting();

	// Stop the weapon from firing
	if (Weapon)
	{
		Weapon->StopFiring();
	}

	// Immediately unregister from coordinator to free attack slot
	UnregisterFromCoordinator();

	// Notify checkpoint subsystem of death (for respawn on player death)
	if (UCheckpointSubsystem* CheckpointSubsystem = GetWorld()->GetSubsystem<UCheckpointSubsystem>())
	{
		CheckpointSubsystem->NotifyNPCDeath(this);
	}

	// increment the team score
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->IncrementTeamScore(TeamByte);
	}

	// broadcast death event (BP can spawn VFX here)
	UE_LOG(LogTemp, Warning, TEXT("ShooterNPC::Die() - Broadcasting OnNPCDeath for %s"), *GetName());
	OnNPCDeath.Broadcast(this);
	OnNPCDeathDetailed.Broadcast(this, LastKillingDamageType, LastKillingDamageCauser);

	// === LOOT DROPS (skipped by finale sequence) ===
	if (!bSuppressDeathDrops)
	{

	// Weapon drop: NPC may drop a melee weapon
	if (DroppedMeleeWeaponClass)
	{
		// Use cached charge (EMF already disabled above)
		const float NPCCharge = CachedNPCCharge;
		// TODO: restore charge-based formula: DropWeaponBaseChance * FMath::Abs(NPCCharge)
		const float DropChance = DropWeaponBaseChance;
		const float Roll = FMath::FRand();

		UE_LOG(LogTemp, Warning, TEXT("[WeaponDrop] %s: EMFCharge=%.2f (PolarityChar::Charge=%.2f), BaseChance=%.2f, Roll=%.3f (need < %.3f)"),
			*GetName(), NPCCharge, GetCharge(), DropWeaponBaseChance, Roll, DropChance);

		if (Roll < DropChance)
		{
			FActorSpawnParameters WeaponSpawnParams;
			WeaponSpawnParams.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

			const FVector SpawnLoc = GetActorLocation() + DropSpawnOffset;
			UE_LOG(LogTemp, Warning, TEXT("[WeaponDrop] Spawning %s at %s"),
				*DroppedMeleeWeaponClass->GetName(), *SpawnLoc.ToString());

			ADroppedMeleeWeapon* DroppedWeapon = GetWorld()->SpawnActor<ADroppedMeleeWeapon>(
				DroppedMeleeWeaponClass, SpawnLoc, GetActorRotation(), WeaponSpawnParams);

			if (DroppedWeapon)
			{
				// If NPC has non-zero charge, transfer it; otherwise keep the default from FieldComponent
				if (!FMath::IsNearlyZero(NPCCharge))
				{
					DroppedWeapon->SetCharge(NPCCharge);
				}
				UE_LOG(LogTemp, Warning, TEXT("[WeaponDrop] SUCCESS — %s spawned, charge=%.2f (NPC was %.2f)"),
					*DroppedWeapon->GetName(), DroppedWeapon->GetCharge(), NPCCharge);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[WeaponDrop] FAILED — SpawnActor returned null!"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[WeaponDrop] %s: Roll %.3f >= Chance %.3f — no drop"), *GetName(), Roll, DropChance);
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[WeaponDrop] %s: No DroppedMeleeWeaponClass set — skipping"), *GetName());
	}

	// Ranged weapon drop: iterate table, first success wins
	if (DroppedRangedWeaponTable.Num() > 0)
	{
		const float NPCChargeForRanged = CachedNPCCharge;

		for (const FDroppedRangedWeaponEntry& Entry : DroppedRangedWeaponTable)
		{
			if (!Entry.DroppedWeaponClass)
			{
				continue;
			}

			const float Roll = FMath::FRand();
			if (Roll < Entry.DropChance)
			{
				FActorSpawnParameters RangedSpawnParams;
				RangedSpawnParams.SpawnCollisionHandlingOverride =
					ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

				const FVector SpawnLoc = GetActorLocation() + DropSpawnOffset;

				ADroppedRangedWeapon* DroppedRanged = GetWorld()->SpawnActor<ADroppedRangedWeapon>(
					Entry.DroppedWeaponClass, SpawnLoc, GetActorRotation(), RangedSpawnParams);

				if (DroppedRanged)
				{
					// If NPC has non-zero charge, transfer it; otherwise keep the default from FieldComponent
					if (!FMath::IsNearlyZero(NPCChargeForRanged))
					{
						DroppedRanged->SetCharge(NPCChargeForRanged);
					}
					UE_LOG(LogTemp, Warning, TEXT("[WeaponDrop] %s: Ranged drop SUCCESS — %s spawned, charge=%.2f (NPC was %.2f)"),
						*GetName(), *Entry.DroppedWeaponClass->GetName(), DroppedRanged->GetCharge(), NPCChargeForRanged);
				}

				break; // Only one ranged drop per death
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[WeaponDrop] %s: Ranged entry %s — Roll %.3f >= Chance %.3f"),
					*GetName(), *Entry.DroppedWeaponClass->GetName(), Roll, Entry.DropChance);
			}
		}
	}

	// Spawn pickup: channeling kills drop armor, prop/drone kills or prop-stunned kills drop health
	UE_LOG(LogTemp, Warning, TEXT("[HP_DROP] %s Die(): bWasChannelingTarget=%d, bStunnedByExplosion=%d, bStunnedByNPCImpact=%d, ArmorPickupClass=%s, HealthPickupClass=%s"),
		*GetName(), bWasChannelingTarget, bStunnedByExplosion, bStunnedByNPCImpact,
		ArmorPickupClass ? *ArmorPickupClass->GetName() : TEXT("NULL"),
		HealthPickupClass ? *HealthPickupClass->GetName() : TEXT("NULL"));
	UE_LOG(LogTemp, Warning, TEXT("[HP_DROP] %s Die(): LastKillingDamageType=%s, LastKillingDamageCauser=%s (Class=%s), bShouldStunOnNPCImpact=%d"),
		*GetName(),
		LastKillingDamageType ? *LastKillingDamageType->GetName() : TEXT("NULL"),
		LastKillingDamageCauser ? *LastKillingDamageCauser->GetName() : TEXT("NULL"),
		LastKillingDamageCauser ? *LastKillingDamageCauser->GetClass()->GetName() : TEXT("NULL"),
		bShouldStunOnNPCImpact);

	// Drone kills always produce health, not armor (even if bWasChannelingTarget was propagated)
	bool bKilledByDrone = Cast<AFlyingDrone>(LastKillingDamageCauser) != nullptr;
	// NPC-on-NPC collision kill (DamageCauser is another ShooterNPC)
	bool bKilledByNPC = Cast<AShooterNPC>(LastKillingDamageCauser) != nullptr;

	if (bWasChannelingTarget && ArmorPickupClass && !bKilledByDrone && !bStunnedByNPCImpact)
	{
		// Armor only for the NPC that was directly channeled, NOT for NPCs hit by the thrown NPC
		UE_LOG(LogTemp, Warning, TEXT("[HP_DROP] %s -> Spawning ARMOR (direct channeling target)"), *GetName());
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<AArmorPickup>(ArmorPickupClass, GetActorLocation(), FRotator::ZeroRotator, SpawnParams);
	}
	else if (HealthPickupClass &&
		(bStunnedByExplosion || AHealthPickup::ShouldDropHealth(LastKillingDamageType, LastKillingDamageCauser)))
	{
		// Prop/drone kill or prop/drone-stunned — full HP drop count
		UE_LOG(LogTemp, Warning, TEXT("[HP_DROP] %s -> Spawning HEALTH (prop/drone/explosion-stun, count=%d)"), *GetName(), HealthPickupDropCount);
		AHealthPickup::SpawnHealthPickups(GetWorld(), HealthPickupClass, GetActorLocation(),
			HealthPickupDropCount, HealthPickupScatterRadius, HealthPickupFloorOffset);
	}
	else if (HealthPickupClass && (bKilledByNPC || bStunnedByNPCImpact))
	{
		// Killed by NPC collision or killed while stunned by NPC impact — reduced HP drop
		UE_LOG(LogTemp, Warning, TEXT("[HP_DROP] %s -> Spawning HEALTH (NPC kill/stun, count=%d, bKilledByNPC=%d, bStunnedByNPCImpact=%d)"), *GetName(), HealthPickupDropCount_NPCKill, bKilledByNPC, bStunnedByNPCImpact);
		AHealthPickup::SpawnHealthPickups(GetWorld(), HealthPickupClass, GetActorLocation(),
			HealthPickupDropCount_NPCKill, HealthPickupScatterRadius, HealthPickupFloorOffset);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[HP_DROP] %s -> NO PICKUP! ShouldDropHealth=%d, bKilledByNPC=%d, bStunnedByNPCImpact=%d, bWasChannelingTarget=%d, HealthPickupClass=%s"),
			*GetName(),
			HealthPickupClass ? AHealthPickup::ShouldDropHealth(LastKillingDamageType, LastKillingDamageCauser) : -1,
			bKilledByNPC, bStunnedByNPCImpact, bWasChannelingTarget,
			HealthPickupClass ? *HealthPickupClass->GetName() : TEXT("NULL"));
	}

	} // end if (!bSuppressDeathDrops)

	// play death sound
	if (DeathSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, DeathSound, GetActorLocation());
	}

	// ============== DEATH MODE DISPATCH ==============

	const FDeathModeConfig& DeathConfig = ResolveDeathConfig();

	switch (DeathConfig.Mode)
	{
	case EDeathMode::Dismemberment:
		SpawnDeathGeometryCollection(DeathConfig);
		DeactivateForDeath(0.5f, /*bHideMesh=*/ true);
		break;

	case EDeathMode::Ragdoll:
		EnableRagdollDeath(DeathConfig);
		DeactivateForDeath(DeathConfig.RagdollDuration, /*bHideMesh=*/ false);
		break;

	case EDeathMode::HideOnly:
	default:
		DeactivateForDeath(0.5f, /*bHideMesh=*/ true);
		break;
	}
}

void AShooterNPC::DeferredDestruction()
{
	if (bIsPooled)
	{
		// Pool mode: hide completely but don't destroy — ArenaManager will recycle
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		return;
	}
	Destroy();
}

// ==================== Pool Recycling ====================

void AShooterNPC::ResetForPool(const FVector& NewLocation, const FRotator& NewRotation)
{
	// --- Core state ---
	bIsDead = false;

	// Reset HP from class defaults
	const AShooterNPC* CDO = GetClass()->GetDefaultObject<AShooterNPC>();
	CurrentHP = CDO->CurrentHP;

	// --- Combat state ---
	bIsShooting = false;
	bWantsToShoot = false;
	bHasAttackPermission = false;
	bInBurstCooldown = false;
	bExternalPermissionGranted = false;
	CurrentBurstShots = 0;
	CurrentAimTarget = nullptr;

	// --- Knockback/stun state ---
	bIsInKnockback = false;
	bIsCaptured = false;
	bIsLaunched = false;
	bWasChannelingTarget = false;
	bIsKnockbackInterpolating = false;
	bStunnedByExplosion = false;
	bStunnedByNPCImpact = false;
	bShouldStunOnNPCImpact = false;
	bCaptureEnabledByStun = false;
	KnockbackDirection = FVector::ZeroVector;
	KnockbackStartPosition = FVector::ZeroVector;
	KnockbackTargetPosition = FVector::ZeroVector;
	KnockbackAttackerPosition = FVector::ZeroVector;
	KnockbackTotalDuration = 0.0f;
	KnockbackElapsedTime = 0.0f;

	// --- Perception ---
	TargetAcquiredTime = -1.0f;
	PerceptionDelayTrackedTarget = nullptr;

	// --- Kill tracking ---
	LastKillingDamageType = nullptr;
	LastKillingDamageCauser = nullptr;
	LastKillingHitDirection = FVector::ZeroVector;
	LastHitReactionTime = -1.0f;
	PreviousPolarity = 0;
	PreviousChargeValue = 0.0f;
	LastWallSlamTime = -1.0f;
	LastEMFProximityTriggerTime = -10.0f;
	PreviousTickVelocity = FVector::ZeroVector;

	// --- Montage state ---
	ActiveCapturedMontage = nullptr;
	ActiveLaunchedMontage = nullptr;

	// --- Clear timers ---
	UWorld* World = GetWorld();
	FTimerManager& TM = World->GetTimerManager();
	TM.ClearTimer(BurstCooldownTimer);
	TM.ClearTimer(PermissionRetryTimer);
	TM.ClearTimer(KnockbackStunTimer);
	TM.ClearTimer(KnockbackInterpTimer);
	TM.ClearTimer(EMFProximityDamageTimer);
	TM.ClearTimer(DeathTimer);

	// --- Mesh: stop physics BEFORE teleport (ragdoll displaces mesh from capsule) ---
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		// 1. Stop ragdoll physics
		MeshComp->SetSimulatePhysics(false);
		MeshComp->SetAllBodiesSimulatePhysics(false);
		MeshComp->bPauseAnims = false;
		MeshComp->bBlendPhysics = false;

		// 2. Snap mesh back to capsule (ragdoll may have displaced it)
		const ACharacter* CharCDO = GetClass()->GetDefaultObject<ACharacter>();
		MeshComp->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		MeshComp->SetRelativeLocation(CharCDO->GetMesh()->GetRelativeLocation());
		MeshComp->SetRelativeRotation(CharCDO->GetMesh()->GetRelativeRotation());

		// 3. Reset animation to clear ragdoll bone poses
		MeshComp->InitAnim(true);

		// 4. Restore visibility and collision
		MeshComp->SetVisibility(true);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->SetComponentTickEnabled(true);
	}

	// --- Teleport ---
	SetActorLocation(NewLocation);
	SetActorRotation(NewRotation);

	// --- Re-enable actor ---
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetActorTickEnabled(true);

	// --- Capsule ---
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}

	// --- Movement ---
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->SetMovementMode(MOVE_Walking);
		MoveComp->SetComponentTickEnabled(true);
	}

	// --- EMF components ---
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetCharge(0.0f);
		EMFVelocityModifier->SetComponentTickEnabled(true);
	}
	if (FieldComponent)
	{
		FieldComponent->SetCharge(0.0f);
		FieldComponent->RegisterWithRegistry();
		FieldComponent->SetComponentTickEnabled(true);
	}

	// --- AI components ---
	if (AccuracyComponent)
	{
		AccuracyComponent->SetComponentTickEnabled(true);
	}
	if (MeleeRetreatComponent)
	{
		MeleeRetreatComponent->SetComponentTickEnabled(true);
	}

	// --- Weapon (destroyed during death — spawn fresh) ---
	if (!Weapon && WeaponClass)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Weapon = World->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);
		if (Weapon)
		{
			Weapon->OnShotFired.AddDynamic(this, &AShooterNPC::OnWeaponShotFired);
		}
	}

	// --- AI Controller (destroyed during death — spawn fresh) ---
	SpawnDefaultController();

	// --- Re-register with subsystems ---
	RegisterWithCoordinator();

	if (UCheckpointSubsystem* CS = World->GetSubsystem<UCheckpointSubsystem>())
	{
		CS->RegisterNPC(this);
	}
	if (UDamageNumbersSubsystem* DNS = World->GetSubsystem<UDamageNumbersSubsystem>())
	{
		DNS->RegisterNPC(this);
	}
	if (UEMFChargeWidgetSubsystem* CWS = World->GetSubsystem<UEMFChargeWidgetSubsystem>())
	{
		CWS->RegisterNPC(this);
	}

	// --- Clear death delegates (ArenaManager will re-bind) ---
	OnNPCDeath.Clear();
	OnNPCDeathDetailed.Clear();

	// --- Charge overlay ---
	if (bUseChargeOverlay)
	{
		UpdateChargeOverlay(0);
	}

	// --- Permanent stun (if configured) ---
	if (bIsPermanentlyStunned)
	{
		ApplyExplosionStun(999999.0f, ReverseChannelingStunMontage);
	}

	UE_LOG(LogTemp, Warning, TEXT("ShooterNPC::ResetForPool — %s recycled at %s"), *GetName(), *NewLocation.ToString());
}

// ==================== Death Effects Implementation ====================

const FDeathModeConfig& AShooterNPC::ResolveDeathConfig() const
{
	if (LastKillingDamageType && DeathConfigOverrides.Num() > 0)
	{
		// Exact match first
		if (const FDeathModeConfig* Config = DeathConfigOverrides.Find(LastKillingDamageType))
		{
			return *Config;
		}

		// Inheritance-aware: walk parent classes (Dropkick -> Melee, EMFWeapon -> Ranged)
		for (const auto& Pair : DeathConfigOverrides)
		{
			if (Pair.Key && LastKillingDamageType->IsChildOf(Pair.Key))
			{
				return Pair.Value;
			}
		}
	}

	return DefaultDeathConfig;
}

void AShooterNPC::SpawnDeathGeometryCollection(const FDeathModeConfig& Config)
{
	if (!DeathGeometryCollection || !GetWorld())
	{
		return;
	}

	const FTransform MeshTransform = GetMesh()->GetComponentTransform();
	const FVector Origin = MeshTransform.GetLocation();
	const FRotator Rotation = MeshTransform.GetRotation().Rotator();
	const FVector Scale = GetMesh()->GetComponentScale();

	// Cache materials once
	TArray<UMaterialInterface*> CachedMaterials;
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		const int32 NumMats = SkelMesh->GetNumMaterials();
		CachedMaterials.Reserve(NumMats);
		for (int32 i = 0; i < NumMats; i++)
		{
			CachedMaterials.Add(SkelMesh->GetMaterial(i));
		}
	}

	const int32 CopyCount = bUltragore ? FMath::Max(2, UltragoreGCCount) : 1;

	for (int32 CopyIdx = 0; CopyIdx < CopyCount; CopyIdx++)
	{
		// Small random offset for extra copies to prevent physics overlap
		FVector SpawnLocation = Origin;
		if (CopyIdx > 0 && UltragoreSpawnOffset > 0.0f)
		{
			SpawnLocation += FMath::VRand() * UltragoreSpawnOffset;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AGeometryCollectionActor* GCActor = GetWorld()->SpawnActor<AGeometryCollectionActor>(
			SpawnLocation, Rotation, SpawnParams);

		if (!GCActor)
		{
			continue;
		}

		UGeometryCollectionComponent* GCComp = GCActor->GetGeometryCollectionComponent();
		if (!GCComp)
		{
			GCActor->Destroy();
			continue;
		}

		GCActor->SetActorScale3D(Scale);

		// Fix collision: gibs should not push pawns
		GCComp->SetCollisionProfileName(RagdollCollisionProfile);
		GCComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		GCComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		GCComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

		// Assign GC asset and initialize physics
		GCComp->SetRestCollection(DeathGeometryCollection);

		// Copy materials from skeletal mesh to GC gibs
		for (int32 i = 0; i < CachedMaterials.Num(); i++)
		{
			if (CachedMaterials[i])
			{
				GCComp->SetMaterial(i, CachedMaterials[i]);
			}
		}

		GCComp->SetSimulatePhysics(true);
		GCComp->RecreatePhysicsState();

		// Break all clusters with massive external strain
		UUniformScalar* StrainField = NewObject<UUniformScalar>(GCActor);
		StrainField->Magnitude = 999999.0f;

		GCComp->ApplyPhysicsField(true,
			EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
			nullptr, StrainField);

		// Scatter pieces with config-driven radial velocity
		URadialVector* RadialVelocity = NewObject<URadialVector>(GCActor);
		RadialVelocity->Magnitude = Config.DismembermentImpulse;
		RadialVelocity->Position = SpawnLocation;

		GCComp->ApplyPhysicsField(true,
			EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity,
			nullptr, RadialVelocity);

		// Angular velocity for tumbling
		URadialVector* AngularVelocity = NewObject<URadialVector>(GCActor);
		AngularVelocity->Magnitude = Config.DismembermentAngularImpulse;
		AngularVelocity->Position = SpawnLocation;

		GCComp->ApplyPhysicsField(true,
			EGeometryCollectionPhysicsTypeEnum::Chaos_AngularVelocity,
			nullptr, AngularVelocity);

		// Directional bias from killing hit direction
		if (!LastKillingHitDirection.IsNearlyZero() && Config.DirectionalBiasMultiplier > 0.0f)
		{
			UUniformVector* DirectionalBias = NewObject<UUniformVector>(GCActor);
			DirectionalBias->Magnitude = Config.DismembermentImpulse * Config.DirectionalBiasMultiplier;
			DirectionalBias->Direction = LastKillingHitDirection;

			GCComp->ApplyPhysicsField(true,
				EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity,
				nullptr, DirectionalBias);
		}

		GCActor->SetLifeSpan(GibLifetime);
	}

	UE_LOG(LogTemp, Log, TEXT("SpawnDeathGC: %s copies=%d impulse=%.0f angular=%.0f dir=[%.2f,%.2f,%.2f]*%.1f"),
		*GetName(), CopyCount, Config.DismembermentImpulse, Config.DismembermentAngularImpulse,
		LastKillingHitDirection.X, LastKillingHitDirection.Y, LastKillingHitDirection.Z,
		Config.DirectionalBiasMultiplier);
}

void AShooterNPC::EnableRagdollDeath(const FDeathModeConfig& Config)
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return;
	}

	// Warn if no Physics Asset — ragdoll won't work without one
	if (!MeshComp->GetPhysicsAsset())
	{
		UE_LOG(LogTemp, Error, TEXT("EnableRagdollDeath: %s has NO Physics Asset! Ragdoll will not work. Assign one in the editor."), *GetName());
		return;
	}

	// 1. Stop movement FIRST so CharacterMovementComponent doesn't fight physics
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
		MoveComp->SetComponentTickEnabled(false);
	}

	// 2. Disable capsule — ragdoll mesh handles collision now
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 3. Stop animations so they don't fight physics
	if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
	{
		AnimInst->Montage_Stop(0.0f);
	}
	MeshComp->bPauseAnims = true;

	// 4. Enable ragdoll physics (requires Physics Asset assigned in editor)
	MeshComp->SetCollisionProfileName(RagdollCollisionProfile);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetAllBodiesSimulatePhysics(true);
	MeshComp->SetSimulatePhysics(true);
	MeshComp->WakeAllRigidBodies();
	MeshComp->bBlendPhysics = true;

	// 5. Damping — prevents breakdancing, makes ragdoll settle naturally
	MeshComp->SetAllBodiesPhysicsBlendWeight(1.0f);
	MeshComp->SetAngularDamping(Config.RagdollAngularDamping);
	MeshComp->SetLinearDamping(Config.RagdollLinearDamping);

	// 6. Apply impulse to root bone only (not all bodies), respects mass
	if (!LastKillingHitDirection.IsNearlyZero())
	{
		const FName RootBone = MeshComp->GetBoneName(0);
		const FVector Impulse = LastKillingHitDirection * Config.RagdollImpulse;
		MeshComp->AddImpulse(Impulse, RootBone, /*bVelChange=*/ false);
	}

	UE_LOG(LogTemp, Log, TEXT("EnableRagdollDeath: %s impulse=%.0f duration=%.1fs dir=[%.2f,%.2f,%.2f]"),
		*GetName(), Config.RagdollImpulse, Config.RagdollDuration,
		LastKillingHitDirection.X, LastKillingHitDirection.Y, LastKillingHitDirection.Z);
}

void AShooterNPC::DeactivateForDeath(float DestructionDelay, bool bHideMesh)
{
	// Disable capsule collision (always — even ragdoll disables capsule, mesh handles it)
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Stop movement
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
		MoveComp->SetComponentTickEnabled(false);
	}

	if (bHideMesh)
	{
		// Dismemberment / HideOnly: mesh no longer needed
		GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GetMesh()->SetVisibility(false);
		GetMesh()->SetComponentTickEnabled(false);
	}
	// Ragdoll: mesh stays visible with physics, don't touch it

	// Disable EMF components
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetCharge(0.0f);
		EMFVelocityModifier->SetComponentTickEnabled(false);
	}
	if (FieldComponent)
	{
		FieldComponent->UnregisterFromRegistry();
		FieldComponent->SetComponentTickEnabled(false);
	}

	// Disable AI components
	if (AccuracyComponent)
	{
		AccuracyComponent->SetComponentTickEnabled(false);
	}
	if (MeleeRetreatComponent)
	{
		MeleeRetreatComponent->SetComponentTickEnabled(false);
	}

	// Pooled NPCs: unregister from subsystems (EndPlay won't run)
	if (bIsPooled)
	{
		UnregisterFromCoordinator();

		if (UDamageNumbersSubsystem* DNS = GetWorld()->GetSubsystem<UDamageNumbersSubsystem>())
		{
			DNS->UnregisterNPC(this);
		}
		if (UEMFChargeWidgetSubsystem* CWS = GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
		{
			CWS->UnregisterNPC(this);
		}
	}

	// Stop AI controller
	if (AController* MyController = GetController())
	{
		if (bIsPooled)
		{
			// Pooled: stop StateTree and destroy controller (will be recreated on recycle)
			if (AShooterAIController* AICtrl = Cast<AShooterAIController>(MyController))
			{
				if (UStateTreeAIComponent* StateTreeComp = AICtrl->FindComponentByClass<UStateTreeAIComponent>())
				{
					StateTreeComp->StopLogic(TEXT("NPCPool"));
				}
			}
			MyController->UnPossess();
			MyController->Destroy();
		}
		else
		{
			MyController->UnPossess();
		}
	}

	// Disable actor tick
	SetActorTickEnabled(false);

	// Destroy weapon
	if (Weapon)
	{
		Weapon->Destroy();
		Weapon = nullptr;
	}

	// Schedule destruction
	GetWorld()->GetTimerManager().SetTimer(DeathTimer, this, &AShooterNPC::DeferredDestruction, DestructionDelay, false);
}

void AShooterNPC::StartShooting(AActor* ActorToShoot, bool bHasExternalPermission)
{
	// Track target acquisition for perception delay
	NotifyTargetAcquired(ActorToShoot);

	// Save the aim target and mark that we want to shoot
	CurrentAimTarget = ActorToShoot;
	bWantsToShoot = true;
	bIsShooting = true;
	bExternalPermissionGranted = bHasExternalPermission;

	// Try to actually start shooting
	TryStartShooting();
}

void AShooterNPC::TryStartShooting()
{
	// Don't shoot if dead or in knockback/captured state
	if (bIsDead || bIsInKnockback)
	{
		StopShooting();
		return;
	}

	// Don't try if we don't want to shoot anymore or target is invalid
	if (!bWantsToShoot || !CurrentAimTarget.IsValid())
	{
		StopPermissionRetryTimer();
		CurrentAimTarget = nullptr;
		return;
	}

	// Wait for perception delay to expire before attacking
	if (IsInPerceptionDelay())
	{
		GetWorldTimerManager().SetTimer(PermissionRetryTimer, this,
			&AShooterNPC::TryStartShooting, 0.1f, false);
		return;
	}

	// Check if in burst cooldown
	if (bInBurstCooldown)
	{
		// Will retry after cooldown ends
		return;
	}

	// Request attack permission from coordinator (always ask, don't cache)
	if (RequestAttackPermission())
	{
		// Got permission - start shooting!
		StopPermissionRetryTimer();
		CurrentBurstShots = 0;

		// Update focus to target (ensures we're aiming at current target position)
		if (AController* MyController = GetController())
		{
			if (AAIController* AIController = Cast<AAIController>(MyController))
			{
				AIController->SetFocus(CurrentAimTarget.Get());
			}
		}

		// Notify coordinator that we're actually attacking now
		if (bUseCoordinator)
		{
			if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(this))
			{
				Coordinator->NotifyAttackStarted(this);
			}
		}

		// Start firing (OnWeaponShotFired will be called via delegate)
		if (Weapon)
		{
			Weapon->StartFiring();
		}
	}
	else
	{
		// No permission yet - start retry timer if not already running
		StartPermissionRetryTimer();
	}
}

void AShooterNPC::StopShooting()
{
	// Clear shooting state
	bIsShooting = false;
	bWantsToShoot = false;
	bExternalPermissionGranted = false;

	// Reset perception delay tracking (target lost)
	TargetAcquiredTime = -1.0f;
	PerceptionDelayTrackedTarget = nullptr;

	// Stop retry timer
	StopPermissionRetryTimer();

	// Signal the weapon
	if (Weapon)
	{
		Weapon->StopFiring();
	}

	// Release attack permission
	ReleaseAttackPermission();
}

void AShooterNPC::NotifyTargetAcquired(AActor* NewTarget)
{
	if (!NewTarget) return;

	// Only reset timer if this is a different target
	if (PerceptionDelayTrackedTarget != NewTarget)
	{
		PerceptionDelayTrackedTarget = NewTarget;
		TargetAcquiredTime = GetWorld()->GetTimeSeconds();
	}
}

bool AShooterNPC::IsInPerceptionDelay() const
{
	if (PerceptionDelay <= 0.0f || TargetAcquiredTime < 0.0f) return false;
	return (GetWorld()->GetTimeSeconds() - TargetAcquiredTime) < PerceptionDelay;
}

bool AShooterNPC::HasLineOfSightTo(AActor* Target) const
{
	if (!Target || !GetWorld())
	{
		return false;
	}

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(Target);

	FHitResult HitResult;
	const FVector Start = GetActorLocation() + FVector(0.0f, 0.0f, 50.0f); // Offset up from center
	const FVector End = Target->GetActorLocation();

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		HitResult,
		Start,
		End,
		ECC_Visibility,
		QueryParams
	);

	// No blocking hit means we have line of sight
	return !bHit;
}

void AShooterNPC::PlayHitReaction(const FVector& DamageDirection)
{
	// Skip hit reaction when stunned — prevents interrupting stun montage
	if (bStunnedByExplosion)
	{
		return;
	}

	// Check cooldown
	float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastHitReactionTime < HitReactionCooldown)
	{
		return;
	}

	// Determine if hit from front or back
	FVector ForwardVector = GetActorForwardVector();
	float DotProduct = FVector::DotProduct(ForwardVector, DamageDirection);

	// Select appropriate montage
	UAnimMontage* MontageToPlay = nullptr;
	if (DotProduct > 0.0f)
	{
		// Hit from front (damage coming towards us)
		MontageToPlay = HitReactionFrontMontage;
	}
	else
	{
		// Hit from behind
		MontageToPlay = HitReactionBackMontage;
	}

	// Play the montage on third person mesh
	if (MontageToPlay)
	{
		if (USkeletalMeshComponent* TPMesh = GetMesh())
		{
			if (UAnimInstance* AnimInstance = TPMesh->GetAnimInstance())
			{
				AnimInstance->Montage_Play(MontageToPlay);
				LastHitReactionTime = CurrentTime;
			}
		}
	}
}

void AShooterNPC::UpdateChargeOverlay(uint8 NewPolarity)
{
	// Don't update if feature is disabled
	if (!bUseChargeOverlay)
	{
		return;
	}

	// Select appropriate material based on polarity
	UMaterialInterface* TargetMaterial = nullptr;

	switch (NewPolarity)
	{
	case 0: // Neutral
		TargetMaterial = NeutralChargeOverlayMaterial;
		break;
	case 1: // Positive
		TargetMaterial = PositiveChargeOverlayMaterial;
		break;
	case 2: // Negative
		TargetMaterial = NegativeChargeOverlayMaterial;
		break;
	default:
		TargetMaterial = NeutralChargeOverlayMaterial;
		break;
	}

	// Apply overlay material to third person mesh
	if (USkeletalMeshComponent* TPMesh = GetMesh())
	{
		TPMesh->SetOverlayMaterial(TargetMaterial);
	}

	// Apply overlay material to first person mesh (NPCs typically don't use this, but included for consistency)
	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		FPMesh->SetOverlayMaterial(TargetMaterial);
	}
}

// ==================== Hit Flash Overlay ====================

UMeshComponent* AShooterNPC::GetHitFlashMeshComponent() const
{
	return GetMesh();
}

void AShooterNPC::StartHitFlash()
{
	if (!HitFlashOverlayMaterial || bIsDead)
	{
		return;
	}

	// If already flashing, just restart the timer (don't overwrite the saved overlay)
	if (bHitFlashActive)
	{
		HitFlashElapsed = 0.0f;
		if (HitFlashDMI)
		{
			const float InitialAlpha = HitFlashCurve ? HitFlashCurve->GetFloatValue(0.0f) : 1.0f;
			HitFlashDMI->SetScalarParameterValue(FName("Opacity"), InitialAlpha);
		}
		return;
	}

	UMeshComponent* FlashMesh = GetHitFlashMeshComponent();
	if (!FlashMesh)
	{
		return;
	}

	// Save current overlay before we replace it
	SavedOverlayBeforeFlash = FlashMesh->GetOverlayMaterial();

	// Create DMI lazily (reuse across flashes)
	if (!HitFlashDMI)
	{
		HitFlashDMI = UMaterialInstanceDynamic::Create(HitFlashOverlayMaterial, this);
	}

	const float InitialAlpha = HitFlashCurve ? HitFlashCurve->GetFloatValue(0.0f) : 1.0f;
	HitFlashDMI->SetScalarParameterValue(FName("Opacity"), InitialAlpha);

	FlashMesh->SetOverlayMaterial(HitFlashDMI);

	HitFlashElapsed = 0.0f;
	bHitFlashActive = true;
}

void AShooterNPC::UpdateHitFlash(float DeltaTime)
{
	if (!bHitFlashActive || !HitFlashDMI)
	{
		return;
	}

	HitFlashElapsed += DeltaTime;

	if (HitFlashElapsed >= HitFlashDuration)
	{
		EndHitFlash();
		return;
	}

	const float NormalizedTime = HitFlashElapsed / HitFlashDuration;
	const float Alpha = HitFlashCurve
		? HitFlashCurve->GetFloatValue(NormalizedTime)
		: 1.0f - NormalizedTime;  // Linear fallback

	HitFlashDMI->SetScalarParameterValue(FName("Opacity"), Alpha);
}

void AShooterNPC::EndHitFlash()
{
	bHitFlashActive = false;
	HitFlashElapsed = -1.0f;

	// Restore saved overlay only if our DMI is still the active overlay
	// (another system like charge overlay may have already overwritten it)
	if (UMeshComponent* FlashMesh = GetHitFlashMeshComponent())
	{
		if (FlashMesh->GetOverlayMaterial() == HitFlashDMI)
		{
			FlashMesh->SetOverlayMaterial(SavedOverlayBeforeFlash);
		}
	}

	SavedOverlayBeforeFlash = nullptr;
}

void AShooterNPC::ApplyExplosionStun(float Duration, UAnimMontage* StunMontage)
{
	// === DEBUG: Show guard check ===
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f,
			(bIsDead || bIsInKnockback || bIsCaptured || bIsLaunched) ? FColor::Red : FColor::Green,
			FString::Printf(TEXT("[STUN GUARD] %s: bIsDead=%d, bIsInKnockback=%d, bIsCaptured=%d, bIsLaunched=%d => %s"),
				*GetName(), bIsDead, bIsInKnockback, bIsCaptured, bIsLaunched,
				(bIsDead || bIsInKnockback || bIsCaptured || bIsLaunched) ? TEXT("BLOCKED!") : TEXT("ALLOWED")));
	}

	// Skip if dead, already stunned, captured, or launched
	if (bIsDead || bIsInKnockback || bIsCaptured || bIsLaunched)
	{
		UE_LOG(LogTemp, Warning, TEXT("[STUN_DEBUG] %s ApplyExplosionStun BLOCKED! bIsDead=%d, bIsInKnockback=%d, bIsCaptured=%d, bIsLaunched=%d"),
			*GetName(), bIsDead, bIsInKnockback, bIsCaptured, bIsLaunched);
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("[STUN_DEBUG] %s ApplyExplosionStun APPLIED (dur=%.2f)"), *GetName(), Duration);

	// Enter knockback state (freezes AI) but without position interpolation
	bIsInKnockback = true;
	bIsKnockbackInterpolating = false;
	bStunnedByExplosion = true;

	// Stop shooting immediately
	StopShooting();

	// Stop AI pathfinding and clear focus to prevent rotation toward player during stun
	if (AController* MyController = GetController())
	{
		if (AAIController* AIController = Cast<AAIController>(MyController))
		{
			if (UPathFollowingComponent* PathComp = AIController->GetPathFollowingComponent())
			{
				PathComp->AbortMove(*this, FPathFollowingResultFlags::UserAbort, FAIRequestID::CurrentRequest, EPathFollowingVelocityMode::Reset);
			}
			AIController->StopMovement();
			AIController->ClearFocus(EAIFocusPriority::Gameplay);
		}
	}

	// Stop character movement and disable CMC entirely to prevent StateTree from re-issuing MoveTo
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();

		// Disable controller-driven rotation so NPC doesn't turn toward player during stun
		bCachedUseControllerDesiredRotation = MoveComp->bUseControllerDesiredRotation;
		MoveComp->bUseControllerDesiredRotation = false;
	}

	// Disable character-level controller rotation
	bCachedUseControllerRotationYaw = bUseControllerRotationYaw;
	bUseControllerRotationYaw = false;

	// Disable EMF forces during stun
	if (bDisableEMFDuringKnockback && EMFVelocityModifier)
	{
		EMFVelocityModifier->SetEnabled(false);
	}

	// Clear any existing knockback timer and schedule recovery
	GetWorld()->GetTimerManager().ClearTimer(KnockbackStunTimer);
	GetWorld()->GetTimerManager().SetTimer(
		KnockbackStunTimer,
		this,
		&AShooterNPC::EndKnockbackStun,
		Duration,
		false
	);

	// Play stun montage (use provided montage, or fallback to KnockbackMontage)
	UAnimMontage* MontageToPlay = StunMontage ? StunMontage : KnockbackMontage.Get();
	if (MontageToPlay)
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			float PlayRate = MontageToPlay->GetPlayLength() > 0.0f
				? MontageToPlay->GetPlayLength() / Duration
				: 1.0f;
			AnimInstance->Montage_Play(MontageToPlay, PlayRate);
		}
	}

	OnStunStart.Broadcast(this, Duration);
}

void AShooterNPC::ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation, bool bKeepEMFEnabled)
{
	// Don't interrupt explosion stun with new knockback
	if (bStunnedByExplosion)
	{
		return;
	}

	// Apply NPC's knockback distance multiplier
	float FinalDistance = Distance * KnockbackDistanceMultiplier;

	// Don't apply knockback if distance is negligible
	if (FinalDistance < 1.0f)
	{
		return;
	}

	// Mark as in knockback state
	bIsInKnockback = true;
	bIsKnockbackInterpolating = true;

	// Stop shooting immediately — burst fire and permission retry don't check knockback
	StopShooting();

	// Store knockback parameters
	KnockbackStartPosition = GetActorLocation();
	KnockbackDirection = InKnockbackDirection.GetSafeNormal();
	KnockbackTargetPosition = KnockbackStartPosition + KnockbackDirection * FinalDistance;
	KnockbackTotalDuration = Duration;
	KnockbackElapsedTime = 0.0f;
	KnockbackAttackerPosition = AttackerLocation;

	// Stop AI pathfinding completely and clear focus to prevent rotation during knockback
	if (AController* MyController = GetController())
	{
		if (AAIController* AIController = Cast<AAIController>(MyController))
		{
			if (UPathFollowingComponent* PathComp = AIController->GetPathFollowingComponent())
			{
				// Use Reset mode to completely stop any velocity from pathfinding
				PathComp->AbortMove(*this, FPathFollowingResultFlags::UserAbort, FAIRequestID::CurrentRequest, EPathFollowingVelocityMode::Reset);
			}
			// Stop any active movement request
			AIController->StopMovement();
			AIController->ClearFocus(EAIFocusPriority::Gameplay);
		}
	}

	// Disable EMF forces during knockback for consistent physics (unless explicitly kept enabled)
	if (bDisableEMFDuringKnockback && !bKeepEMFEnabled && EMFVelocityModifier)
	{
		EMFVelocityModifier->SetEnabled(false);
	}

	// Disable CharacterMovementComponent during interpolation
	if (UCharacterMovementComponent* CharMovement = GetCharacterMovement())
	{
		// Save original friction if not already saved
		if (CachedGroundFriction == 8.0f)
		{
			CachedGroundFriction = CharMovement->GroundFriction;
		}

		// Set low friction for any residual sliding
		CharMovement->GroundFriction = KnockbackGroundFriction;

		// Stop any current movement
		CharMovement->StopActiveMovement();
		CharMovement->Velocity = FVector::ZeroVector;

		// Disable controller-driven rotation so NPC doesn't turn toward player during knockback
		bCachedUseControllerDesiredRotation = CharMovement->bUseControllerDesiredRotation;
		CharMovement->bUseControllerDesiredRotation = false;
	}

	// Disable character-level controller rotation
	bCachedUseControllerRotationYaw = bUseControllerRotationYaw;
	bUseControllerRotationYaw = false;

	// Clear any existing timers
	GetWorld()->GetTimerManager().ClearTimer(KnockbackStunTimer);

	// Schedule stun end (slightly longer than knockback duration to ensure interpolation completes)
	GetWorld()->GetTimerManager().SetTimer(
		KnockbackStunTimer,
		this,
		&AShooterNPC::EndKnockbackStun,
		Duration + 0.1f,
		false
	);

	// Play knockback animation montage if available
	if (KnockbackMontage)
	{
		UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
		if (AnimInstance && !AnimInstance->Montage_IsPlaying(KnockbackMontage))
		{
			// Calculate play rate to match knockback duration
			// Montage is 1 second long, so PlayRate = 1.0 / Duration
			float PlayRate = 1.0f / Duration;
			AnimInstance->Montage_Play(KnockbackMontage, PlayRate);
		}
	}

#if WITH_EDITOR
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
			FString::Printf(TEXT("Distance Knockback: Dir=(%.2f,%.2f,%.2f), Dist=%.0f, Duration=%.2f"),
				KnockbackDirection.X, KnockbackDirection.Y, KnockbackDirection.Z,
				FinalDistance, Duration));
	}
#endif
}

void AShooterNPC::ApplyKnockbackVelocity(const FVector& KnockbackVelocity, float StunDuration)
{
	// Convert velocity-based knockback to distance-based
	// Estimate distance based on velocity magnitude and stun duration
	float Speed = KnockbackVelocity.Size();
	FVector Direction = KnockbackVelocity.GetSafeNormal();

	// Estimate distance as speed * duration (simplified)
	float EstimatedDistance = Speed * StunDuration;

	// Use the new distance-based system
	ApplyKnockback(Direction, EstimatedDistance, StunDuration);
}

void AShooterNPC::UpdateKnockbackInterpolation(float DeltaTime)
{
	if (!bIsKnockbackInterpolating || KnockbackTotalDuration <= 0.0f)
	{
		return;
	}

	// Update elapsed time
	KnockbackElapsedTime += DeltaTime;

	// Calculate interpolation alpha
	float Alpha = FMath::Clamp(KnockbackElapsedTime / KnockbackTotalDuration, 0.0f, 1.0f);

	// Constant speed for 90% of the path, then ease out for the last 10%
	float EasedAlpha;
	if (Alpha < 0.9f)
	{
		// Linear interpolation (constant speed) for first 90%
		EasedAlpha = Alpha;
	}
	else
	{
		// Ease out for last 10% - remap 0.9-1.0 to smooth deceleration
		float LastSegmentAlpha = (Alpha - 0.9f) / 0.1f; // 0.0 to 1.0 in the last segment
		float EasedSegment = FMath::InterpEaseOut(0.0f, 0.1f, LastSegmentAlpha, 2.0f);
		EasedAlpha = 0.9f + EasedSegment;
	}

	// Calculate next position with horizontal knockback
	FVector CurrentPos = GetActorLocation();
	FVector NextPos = FMath::Lerp(KnockbackStartPosition, KnockbackTargetPosition, EasedAlpha);

	// Apply gravity (only if not on ground)
	if (UCharacterMovementComponent* CharMovement = GetCharacterMovement())
	{
		if (!CharMovement->IsMovingOnGround())
		{
			// Get gravity from movement component
			float GravityZ = CharMovement->GetGravityZ();

			// Apply gravity delta for this frame
			FVector GravityDelta = FVector(0.0f, 0.0f, GravityZ * DeltaTime);
			NextPos += GravityDelta;
		}
	}

	// ==================== NPC-NPC Collision Detection (Overlap Sweep) ====================
	// Check for NPC collision BEFORE SetActorLocation() to get accurate velocity
	// OnCapsuleHit fires AFTER physics contact when velocity is already dampened
	if (bEnableNPCCollision && GetWorld())
	{
		UCapsuleComponent* Capsule = GetCapsuleComponent();
		if (Capsule)
		{
			float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
			float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();

			// Use slightly larger radius for early detection
			float DetectionRadius = CapsuleRadius * 1.1f;

			FCollisionQueryParams QueryParams;
			QueryParams.AddIgnoredActor(this);
			QueryParams.bTraceComplex = false;

			TArray<FOverlapResult> Overlaps;
			bool bHasOverlaps = GetWorld()->OverlapMultiByChannel(
				Overlaps,
				NextPos,
				FQuat::Identity,
				ECC_Pawn,
				FCollisionShape::MakeCapsule(DetectionRadius, CapsuleHalfHeight),
				QueryParams
			);

			if (bHasOverlaps)
			{
				for (const FOverlapResult& Overlap : Overlaps)
				{
					AShooterNPC* OtherNPC = Cast<AShooterNPC>(Overlap.GetActor());
					if (OtherNPC && !OtherNPC->IsDead())
					{
						// Calculate knockback velocity from Distance/Duration (not PreviousTickVelocity)
						// This gives us the TRUE knockback speed before any physics dampening
						float TotalDistance = FVector::Dist(KnockbackStartPosition, KnockbackTargetPosition);
						float KnockbackSpeed = (KnockbackTotalDuration > 0.0f) ? TotalDistance / KnockbackTotalDuration : 0.0f;

						UE_LOG(LogTemp, Warning, TEXT("[NPC Collision SWEEP] %s detected NPC %s - KnockbackSpeed=%.0f (from Distance=%.0f / Duration=%.2f), MinVelocity=%.0f"),
							*GetName(), *OtherNPC->GetName(), KnockbackSpeed, TotalDistance, KnockbackTotalDuration, NPCCollisionMinVelocity);

						if (KnockbackSpeed >= NPCCollisionMinVelocity)
						{
							// Calculate collision point (midpoint between the two NPCs)
							FVector CollisionPoint = (CurrentPos + OtherNPC->GetActorLocation()) * 0.5f;

							UE_LOG(LogTemp, Warning, TEXT("[NPC Collision SWEEP] TRIGGERING elastic collision! Speed=%.0f >= Threshold=%.0f"),
								KnockbackSpeed, NPCCollisionMinVelocity);

							// Handle elastic collision with computed knockback velocity
							HandleElasticNPCCollisionWithSpeed(OtherNPC, CollisionPoint, KnockbackSpeed);
							return; // Stop interpolation after collision
						}
						else
						{
							UE_LOG(LogTemp, Warning, TEXT("[NPC Collision SWEEP] Too slow: %.0f < %.0f"),
								KnockbackSpeed, NPCCollisionMinVelocity);
						}
					}
				}
			}
		}
	}

	// Check for wall collision before moving
	FHitResult WallHit;
	if (CheckKnockbackWallCollision(CurrentPos, NextPos, WallHit))
	{
		// Wall hit - trigger damage and stop knockback
		HandleKnockbackWallHit(WallHit);
		return;
	}

	// Move the character to the interpolated position
	SetActorLocation(NextPos, true);

	// Rotate to face attacker during knockback (if attacker position was provided)
	if (!KnockbackAttackerPosition.IsZero())
	{
		FVector ToAttacker = KnockbackAttackerPosition - NextPos;
		ToAttacker.Z = 0.0f; // Keep rotation horizontal
		if (!ToAttacker.IsNearlyZero())
		{
			FRotator TargetRotation = ToAttacker.Rotation();
			SetActorRotation(TargetRotation);
		}
	}

	// Update velocity for visual purposes (affects animations, etc.)
	if (UCharacterMovementComponent* CharMovement = GetCharacterMovement())
	{
		// Calculate apparent velocity for this frame
		FVector FrameVelocity = (NextPos - CurrentPos) / DeltaTime;
		CharMovement->Velocity = FrameVelocity;
	}

	// Check if knockback is complete
	if (Alpha >= 1.0f)
	{
		bIsKnockbackInterpolating = false;

		// Stop velocity
		if (UCharacterMovementComponent* CharMovement = GetCharacterMovement())
		{
			CharMovement->Velocity = FVector::ZeroVector;
		}
	}
}

bool AShooterNPC::CheckKnockbackWallCollision(const FVector& CurrentPos, const FVector& NextPos, FHitResult& OutHit)
{
	if (!GetWorld())
	{
		return false;
	}

	// Get capsule dimensions for sweep
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule)
	{
		return false;
	}

	float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();

	// Set up collision query
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.bTraceComplex = false;

	// Sweep from current to next position
	bool bHit = GetWorld()->SweepSingleByChannel(
		OutHit,
		CurrentPos,
		NextPos,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight),
		QueryParams
	);

	if (bHit)
	{
		// Check if this is a wall (not floor - normal pointing mostly horizontal)
		// Walls have normals with small Z component, floors have large positive Z
		float NormalZ = FMath::Abs(OutHit.ImpactNormal.Z);

		// Consider it a wall if Z component is less than 0.7 (about 45 degrees)
		// This allows wall slams on walls and steep slopes, but not on flat ground
		return NormalZ < 0.7f;
	}

	return false;
}

void AShooterNPC::HandleKnockbackWallHit(const FHitResult& WallHit)
{
	// Calculate impact velocity based on remaining distance and time
	float RemainingDistance = FVector::Dist(GetActorLocation(), KnockbackTargetPosition);
	float RemainingTime = KnockbackTotalDuration - KnockbackElapsedTime;
	float ImpactSpeed = (RemainingTime > 0.0f) ? RemainingDistance / RemainingTime : 0.0f;

	// Calculate incoming velocity vector
	FVector IncomingVelocity = KnockbackDirection * ImpactSpeed;

	// Calculate perpendicular velocity component (how hard we hit the wall)
	const FVector Normal = WallHit.ImpactNormal;
	float DotProduct = FVector::DotProduct(IncomingVelocity, Normal);
	float PerpendicularVelocity = FMath::Abs(DotProduct);

	// Move to wall position with offset based on capsule radius to avoid penetration
	float CapsuleRadius = GetCapsuleComponent()->GetScaledCapsuleRadius();
	FVector WallPosition = WallHit.Location + Normal * (CapsuleRadius + 5.0f);
	SetActorLocation(WallPosition, false);

	// Apply wall slam damage first (if strong enough)
	if (PerpendicularVelocity >= WallSlamVelocityThreshold)
	{
		float ExcessVelocity = PerpendicularVelocity - WallSlamVelocityThreshold;
		float WallSlamDamage = (ExcessVelocity / 100.0f) * WallSlamDamagePerVelocity;

		if (WallSlamDamage > 0.0f)
		{
			FDamageEvent DamageEvent;
			DamageEvent.DamageTypeClass = UDamageType_Wallslam::StaticClass();
			TakeDamage(WallSlamDamage, DamageEvent, nullptr, this);

			if (WallSlamSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, WallSlamSound, WallHit.ImpactPoint);
			}

			if (WallSlamVFX)
			{
				// Build rotation so VFX lies flat on wall surface
				FRotator VFXRotation = UKismetMathLibrary::MakeRotFromYZ(WallHit.ImpactNormal, FVector::UpVector);
				VFXRotation.Roll += 90.0f;  // Rotate to lie flat on wall

				// Offset slightly away from wall to prevent clipping
				FVector VFXLocation = WallHit.ImpactPoint + WallHit.ImpactNormal * 5.0f;

				UNiagaraFunctionLibrary::SpawnSystemAtLocation(
					GetWorld(),
					WallSlamVFX,
					VFXLocation,
					VFXRotation,
					FVector(WallSlamVFXScale),
					true,
					true,
					ENCPoolMethod::None
				);
			}
		}
	}

	// Check if NPC died from wall slam
	if (bIsDead)
	{
		bIsKnockbackInterpolating = false;
		return;
	}

	// Calculate reflected velocity for bounce
	FVector ReflectedVelocity = IncomingVelocity - (1.0f + WallBounceElasticity) * DotProduct * Normal;
	float ReflectedSpeed = ReflectedVelocity.Size();

	// Check if we should bounce or stop
	if (bEnableWallBounce && ReflectedSpeed >= WallBounceMinVelocity)
	{
		// Continue knockback in reflected direction
		FVector ReflectedDirection = ReflectedVelocity.GetSafeNormal();

		// Calculate remaining duration based on reflected speed
		float ReflectedDistance = ReflectedSpeed * RemainingTime * WallBounceElasticity;
		float BounceDuration = RemainingTime * WallBounceElasticity;

		// Update knockback parameters for bounce
		KnockbackStartPosition = WallPosition;
		KnockbackDirection = ReflectedDirection;
		KnockbackTargetPosition = WallPosition + ReflectedDirection * ReflectedDistance;
		KnockbackElapsedTime = 0.0f;
		KnockbackTotalDuration = BounceDuration;
		bIsKnockbackInterpolating = true;
		// bIsInKnockback stays true - movement remains blocked

		// Ensure AI movement stays blocked during bounce
		if (AController* MyController = GetController())
		{
			if (AAIController* AIController = Cast<AAIController>(MyController))
			{
				if (UPathFollowingComponent* PathComp = AIController->GetPathFollowingComponent())
				{
					PathComp->AbortMove(*this, FPathFollowingResultFlags::UserAbort, FAIRequestID::CurrentRequest, EPathFollowingVelocityMode::Reset);
				}
				AIController->StopMovement();
			}
		}

		// Clear stun timer and set new one for bounce duration
		GetWorld()->GetTimerManager().ClearTimer(KnockbackStunTimer);
		GetWorld()->GetTimerManager().SetTimer(
			KnockbackStunTimer,
			this,
			&AShooterNPC::EndKnockbackStun,
			BounceDuration + 0.1f,
			false
		);

#if WITH_EDITOR
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
				FString::Printf(TEXT("WALL BOUNCE! InSpeed=%.0f, OutSpeed=%.0f, Elasticity=%.2f"),
					ImpactSpeed, ReflectedSpeed, WallBounceElasticity));
		}
#endif
	}
	else
	{
		// Speed too low for bounce - stop knockback and restore state immediately
		bIsKnockbackInterpolating = false;

		if (UCharacterMovementComponent* CharMovement = GetCharacterMovement())
		{
			CharMovement->Velocity = FVector::ZeroVector;
		}

		// Clear the stun timer and end knockback immediately
		GetWorld()->GetTimerManager().ClearTimer(KnockbackStunTimer);
		EndKnockbackStun();

#if WITH_EDITOR
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow,
				FString::Printf(TEXT("Wall hit - stopping (speed %.0f < min %.0f)"),
					ReflectedSpeed, WallBounceMinVelocity));
		}
#endif
	}
}

void AShooterNPC::HandleElasticNPCCollision(AShooterNPC* OtherNPC, const FVector& CollisionPoint)
{
	// Legacy function - use computed knockback speed from Distance/Duration
	if (!OtherNPC)
	{
		return;
	}

	// Calculate knockback speed from stored knockback parameters
	float TotalDistance = FVector::Dist(KnockbackStartPosition, KnockbackTargetPosition);
	float KnockbackSpeed = (KnockbackTotalDuration > 0.0f) ? TotalDistance / KnockbackTotalDuration : PreviousTickVelocity.Size();

	HandleElasticNPCCollisionWithSpeed(OtherNPC, CollisionPoint, KnockbackSpeed);
}

void AShooterNPC::HandleElasticNPCCollisionWithSpeed(AShooterNPC* OtherNPC, const FVector& CollisionPoint, float ImpactSpeed)
{
	if (!OtherNPC)
	{
		return;
	}

	// ==================== Explosion-Like Elastic Collision ====================
	// Both NPCs get knocked back in opposite directions with multiplied original velocity

	// Tag other NPC for armor drop if either NPC was a channeling target
	// (must be done BEFORE delayed damage, as bIsLaunched gets cleared after this function)
	if (bWasChannelingTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[HP_DROP] NPC Collision: PROPAGATING bWasChannelingTarget from %s -> %s"), *GetName(), *OtherNPC->GetName());
		OtherNPC->bWasChannelingTarget = true;
	}
	if (OtherNPC->bWasChannelingTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[HP_DROP] NPC Collision: PROPAGATING bWasChannelingTarget from %s -> %s"), *OtherNPC->GetName(), *GetName());
		bWasChannelingTarget = true;
	}

	UE_LOG(LogTemp, Warning, TEXT("[NPC Collision] HandleElasticNPCCollisionWithSpeed: %s -> %s, ImpactSpeed=%.0f"),
		*GetName(), *OtherNPC->GetName(), ImpactSpeed);

	// Calculate collision direction (from me to other NPC)
	FVector CollisionDirection = (OtherNPC->GetActorLocation() - GetActorLocation()).GetSafeNormal();

	// ==================== EMF Discharge Detection ====================
	// Check if this is an EMF-induced collision (opposite charges)
	bool bIsEMFDischarge = false;
	float MyCharge = 0.0f;
	float OtherCharge = 0.0f;
	float TotalChargeMagnitude = 0.0f;

	if (bEnableEMFProximityKnockback && EMFVelocityModifier && OtherNPC->EMFVelocityModifier)
	{
		MyCharge = EMFVelocityModifier->GetCharge();
		OtherCharge = OtherNPC->EMFVelocityModifier->GetCharge();

		// Check for opposite charges (product is negative)
		if (MyCharge * OtherCharge < 0.0f)
		{
			bIsEMFDischarge = true;
			TotalChargeMagnitude = FMath::Abs(MyCharge) + FMath::Abs(OtherCharge);

			UE_LOG(LogTemp, Warning, TEXT("[NPC Collision] EMF DISCHARGE detected! MyCharge=%.1f, OtherCharge=%.1f, Total=%.1f"),
				MyCharge, OtherCharge, TotalChargeMagnitude);
		}
	}

	// Calculate collision damage based on impact speed (Kinetic - Wallslam category)
	float KineticCollisionDamage = 0.0f;
	if (ImpactSpeed >= WallSlamVelocityThreshold)
	{
		float ExcessVelocity = ImpactSpeed - WallSlamVelocityThreshold;
		float BaseDamage = (ExcessVelocity / 100.0f) * WallSlamDamagePerVelocity;
		KineticCollisionDamage = BaseDamage * NPCCollisionDamageMultiplier;
	}

	// EMF discharge damage based on charge magnitude (EMF category)
	float EMFCollisionDamage = 0.0f;
	if (bIsEMFDischarge && EMFProximityDamage > 0.0f)
	{
		// Scale damage by total charge magnitude
		EMFCollisionDamage = EMFProximityDamage * (TotalChargeMagnitude / 100.0f);
		EMFCollisionDamage = FMath::Max(EMFCollisionDamage, EMFProximityDamage); // At least base damage

		UE_LOG(LogTemp, Warning, TEXT("[NPC Collision] EMF Discharge damage added: %.1f (from charge=%.1f)"),
			EMFCollisionDamage, TotalChargeMagnitude);
	}

	// Total collision damage for knockback calculations
	float CollisionDamage = KineticCollisionDamage + EMFCollisionDamage;

	// Calculate knockback duration for both NPCs based on new velocity
	// Use distance-based formula from ApplyKnockback
	float NewSpeed = ImpactSpeed * NPCCollisionImpulseMultiplier;

	// Add EMF discharge bonus impulse if applicable
	if (bIsEMFDischarge)
	{
		float EMFBonusSpeed = TotalChargeMagnitude * EMFDischargeImpulsePerCharge;
		NewSpeed += EMFBonusSpeed;

		UE_LOG(LogTemp, Warning, TEXT("[NPC Collision] EMF Discharge bonus: %.0f -> %.0f (bonus=%.0f from charge=%.1f)"),
			ImpactSpeed * NPCCollisionImpulseMultiplier, NewSpeed, EMFBonusSpeed, TotalChargeMagnitude);
	}

	float KnockbackDistance = (NewSpeed * NewSpeed) / 2000.0f; // Simple physics approximation
	float KnockbackDuration = KnockbackDistance / FMath::Max(NewSpeed, 1.0f);
	KnockbackDuration = FMath::Clamp(KnockbackDuration, 0.3f, 2.0f); // Reasonable bounds

	// === IMPACT FREEZE: stop both NPCs and reduce post-impact scatter ===
	if (UCharacterMovementComponent* MyMoveComp = GetCharacterMovement())
	{
		MyMoveComp->StopMovementImmediately();
	}
	if (UCharacterMovementComponent* OtherMoveComp = OtherNPC->GetCharacterMovement())
	{
		OtherMoveComp->StopMovementImmediately();
	}

	// Reduce scatter so NPCs die/stagger near collision point
	KnockbackDistance *= NPCCollisionPostImpactKnockbackMultiplier;

	UE_LOG(LogTemp, Warning, TEXT("[NPC Collision] Applying knockback to SELF %s: Dir=(%.1f,%.1f,%.1f), Dist=%.0f, Dur=%.2f"),
		*GetName(), -CollisionDirection.X, -CollisionDirection.Y, -CollisionDirection.Z,
		KnockbackDistance, KnockbackDuration);

	// Apply knockback to myself (backwards)
	ApplyKnockback(-CollisionDirection, KnockbackDistance, KnockbackDuration, OtherNPC->GetActorLocation());

	// If this NPC was launched via reverse channeling, stun the target instead of knockback.
	// Uses bShouldStunOnNPCImpact which persists even after bIsLaunched is cleared (e.g. speed dropped).

	// === DEBUG: Show stun flag state ===
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Yellow,
			FString::Printf(TEXT("[RC STUN] %s: bShouldStunOnNPCImpact=%d, bIsLaunched=%d, bIsInKnockback=%d | Target %s: bIsInKnockback=%d, bIsCaptured=%d, bIsLaunched=%d, bIsDead=%d"),
				*GetName(), bShouldStunOnNPCImpact, bIsLaunched, bIsInKnockback,
				*OtherNPC->GetName(), OtherNPC->bIsInKnockback, OtherNPC->bIsCaptured, OtherNPC->bIsLaunched, OtherNPC->bIsDead));
	}

	if (bShouldStunOnNPCImpact)
	{
		bShouldStunOnNPCImpact = false; // One-shot: only stun on first NPC-NPC collision

		UE_LOG(LogTemp, Warning, TEXT("[NPC Collision] Applying REVERSE CHANNELING STUN to %s for %.1fs"),
			*OtherNPC->GetName(), ReverseChannelingStunDuration);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Green,
				FString::Printf(TEXT("[RC STUN] >>> Calling ApplyExplosionStun on %s (dur=%.1f)"),
					*OtherNPC->GetName(), ReverseChannelingStunDuration));
		}

		OtherNPC->ApplyExplosionStun(ReverseChannelingStunDuration, ReverseChannelingStunMontage);
		OtherNPC->bStunnedByNPCImpact = true; // Track NPC-sourced stun for reduced HP pickup drop
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[NPC Collision] Applying knockback to OTHER NPC %s: Dir=(%.1f,%.1f,%.1f), Dist=%.0f, Dur=%.2f"),
			*OtherNPC->GetName(), CollisionDirection.X, CollisionDirection.Y, CollisionDirection.Z,
			KnockbackDistance, KnockbackDuration);

		// Apply knockback to other NPC (forwards)
		OtherNPC->ApplyKnockback(CollisionDirection, KnockbackDistance, KnockbackDuration, GetActorLocation());
	}

	// Apply damage to both NPCs with a very short delay
	// NPCs are stopped by impact freeze, so minimal delay needed for knockback state init
	// Pass nullptr as DamageCauser to bypass friendly fire check (NPC-NPC collision damage is intentional)
	// Split damage by type: Kinetic (Wallslam) and EMF (EMFProximity)
	if (CollisionDamage > 0.0f)
	{
		const float DamageDelay = 0.02f;
		float KineticDamage = KineticCollisionDamage;
		float EMFDamage = EMFCollisionDamage;

		// Delay damage to self (DamageCauser = OtherNPC — who hit us)
		// Note: don't check IsDead() on Causer — pointer is still valid (deferred destruction 0.5s)
		// and we need accurate DamageCauser for health pickup drop logic
		FTimerHandle SelfDamageTimer;
		GetWorld()->GetTimerManager().SetTimer(
			SelfDamageTimer,
			[this, OtherNPC, KineticDamage, EMFDamage]()
			{
				if (!bIsDead)
				{
					AActor* Causer = static_cast<AActor*>(OtherNPC);
					// Apply Kinetic damage (Wallslam type)
					if (KineticDamage > 0.0f)
					{
						FDamageEvent KineticEvent;
						KineticEvent.DamageTypeClass = UDamageType_Wallslam::StaticClass();
						TakeDamage(KineticDamage, KineticEvent, nullptr, Causer);
					}
					// Apply EMF damage (EMFProximity type)
					if (EMFDamage > 0.0f)
					{
						FDamageEvent EMFEvent;
						EMFEvent.DamageTypeClass = UDamageType_EMFProximity::StaticClass();
						TakeDamage(EMFDamage, EMFEvent, nullptr, Causer);
					}
				}
			},
			DamageDelay,
			false
		);

		// Delay damage to other NPC (DamageCauser = this — we hit them)
		FTimerHandle OtherDamageTimer;
		AShooterNPC* Self = this;
		GetWorld()->GetTimerManager().SetTimer(
			OtherDamageTimer,
			[Self, OtherNPC, KineticDamage, EMFDamage]()
			{
				if (OtherNPC && !OtherNPC->IsDead())
				{
					AActor* Causer = static_cast<AActor*>(Self);
					// Apply Kinetic damage (Wallslam type)
					if (KineticDamage > 0.0f)
					{
						FDamageEvent KineticEvent;
						KineticEvent.DamageTypeClass = UDamageType_Wallslam::StaticClass();
						OtherNPC->TakeDamage(KineticDamage, KineticEvent, nullptr, Causer);
					}
					// Apply EMF damage (EMFProximity type)
					if (EMFDamage > 0.0f)
					{
						FDamageEvent EMFEvent;
						EMFEvent.DamageTypeClass = UDamageType_EMFProximity::StaticClass();
						OtherNPC->TakeDamage(EMFDamage, EMFEvent, nullptr, Causer);
					}
				}
			},
			DamageDelay,
			false
		);

		UE_LOG(LogTemp, Warning, TEXT("[NPC Collision] Applying damage: Kinetic=%.1f, EMF=%.1f to both NPCs (delayed %.2fs)"),
			KineticDamage, EMFDamage, DamageDelay);
	}

	// ==================== EMF Discharge Effects ====================
	if (bIsEMFDischarge)
	{
		// Neutralize charges on both NPCs
		if (EMFVelocityModifier)
		{
			UE_LOG(LogTemp, Warning, TEXT("[NPC Collision] Neutralizing charge: %s (%.1f -> 0)"), *GetName(), MyCharge);
			EMFVelocityModifier->SetCharge(0.0f);
		}
		if (OtherNPC->EMFVelocityModifier)
		{
			UE_LOG(LogTemp, Warning, TEXT("[NPC Collision] Neutralizing charge: %s (%.1f -> 0)"), *OtherNPC->GetName(), OtherCharge);
			OtherNPC->EMFVelocityModifier->SetCharge(0.0f);
		}

		// Play EMF discharge sound
		if (EMFDischargeSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, EMFDischargeSound, CollisionPoint);
		}

		// Spawn EMF discharge VFX
		if (EMFDischargeVFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				GetWorld(),
				EMFDischargeVFX,
				CollisionPoint,
				FRotator::ZeroRotator,
				FVector(EMFDischargeVFXScale),
				true,  // bAutoDestroy
				true,  // bAutoActivate
				ENCPoolMethod::None
			);
		}
	}
	else
	{
		// Regular collision - use wall slam effects
		// Play sound at collision point
		if (WallSlamSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, WallSlamSound, CollisionPoint);
		}

		// Spawn VFX at collision point
		if (WallSlamVFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				GetWorld(),
				WallSlamVFX,
				CollisionPoint,
				FRotator::ZeroRotator,
				FVector(WallSlamVFXScale),
				true,  // bAutoDestroy
				true,  // bAutoActivate
				ENCPoolMethod::None
			);
		}
	}

	// NPC-NPC specific impact shockwave (always spawns, separate from EMF/wallslam effects)
	if (NPCCollisionImpactVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			NPCCollisionImpactVFX,
			CollisionPoint,
			FRotator::ZeroRotator,
			FVector(NPCCollisionImpactVFXScale),
			true,
			true,
			ENCPoolMethod::None
		);
	}
	if (NPCCollisionImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, NPCCollisionImpactSound, CollisionPoint);
	}

	UE_LOG(LogTemp, Warning, TEXT("[NPC Collision] COLLISION COMPLETE! ImpactSpeed=%.0f, NewSpeed=%.0f, Damage=%.1f, Duration=%.2fs"),
		ImpactSpeed, NewSpeed, CollisionDamage, KnockbackDuration);
}

void AShooterNPC::CheckEMFProximityCollision()
{
	// Don't check if feature is disabled or already in knockback
	if (!bEnableEMFProximityKnockback || bIsInKnockback || !EMFVelocityModifier)
	{
		return;
	}

	// Check cooldown to prevent spam
	float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastEMFProximityTriggerTime < EMFProximityTriggerCooldown)
	{
		return;
	}

	// Get current EMF acceleration
	FVector CurrentAcceleration = EMFVelocityModifier->CurrentAcceleration;
	float AccelerationMagnitude = CurrentAcceleration.Size();

	// Check if acceleration exceeds threshold
	if (AccelerationMagnitude < EMFProximityAccelerationThreshold)
	{
		return;
	}

	// Find nearby NPCs using overlap sphere
	// We need to find the NPC that's causing this strong EMF force
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.bTraceComplex = false;

	// Use a large search radius since EMF forces work at distance
	float SearchRadius = 2000.0f; // 20 meters

	bool bFoundOverlaps = GetWorld()->OverlapMultiByChannel(
		Overlaps,
		GetActorLocation(),
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(SearchRadius),
		QueryParams
	);

	if (!bFoundOverlaps)
	{
		return;
	}

	// Get my charge
	float MyCharge = EMFVelocityModifier->GetCharge();

	// Charges too weak - skip
	if (FMath::Abs(MyCharge) < KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Look for NPCs with opposite charge
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AShooterNPC* OtherNPC = Cast<AShooterNPC>(Overlap.GetActor());
		if (!OtherNPC || OtherNPC->IsDead())
		{
			continue;
		}

		// Get other NPC's charge
		UEMFVelocityModifier* OtherEMF = OtherNPC->EMFVelocityModifier;
		if (!OtherEMF)
		{
			continue;
		}

		float OtherCharge = OtherEMF->GetCharge();

		// Check for opposite charges (product is negative)
		if (MyCharge * OtherCharge >= 0)
		{
			continue; // Same sign or one is neutral
		}

		// Use pointer comparison to ensure only ONE NPC triggers the event
		// This prevents double-activation when both NPCs detect each other
		if (this > OtherNPC)
		{
			continue; // Let the "lower" pointer handle it
		}

		// Found a valid target - trigger EMF proximity knockback
		UE_LOG(LogTemp, Warning, TEXT("[EMF Proximity] %s detected opposite charge NPC %s - Acceleration=%.0f >= Threshold=%.0f, MyCharge=%.1f, OtherCharge=%.1f"),
			*GetName(), *OtherNPC->GetName(), AccelerationMagnitude, EMFProximityAccelerationThreshold, MyCharge, OtherCharge);

		TriggerEMFProximityKnockback(OtherNPC);
		return; // Only trigger once per check
	}
}

void AShooterNPC::TriggerEMFProximityKnockback(AShooterNPC* OtherNPC)
{
	if (!OtherNPC || !EMFVelocityModifier || !OtherNPC->EMFVelocityModifier)
	{
		return;
	}

	// Mark cooldown for both NPCs
	float CurrentTime = GetWorld()->GetTimeSeconds();
	LastEMFProximityTriggerTime = CurrentTime;
	OtherNPC->LastEMFProximityTriggerTime = CurrentTime;

	// Calculate direction between NPCs (attraction - towards each other)
	FVector ToOther = (OtherNPC->GetActorLocation() - GetActorLocation()).GetSafeNormal();

	UE_LOG(LogTemp, Warning, TEXT("[EMF Proximity] Triggering attraction knockback: %s <-> %s, Distance=%.0f, Duration=%.2f"),
		*GetName(), *OtherNPC->GetName(), EMFProximityKnockbackDistance, EMFProximityKnockbackDuration);

	// Apply knockback to BOTH NPCs towards each other
	// Keep EMF enabled so attraction force continues to pull them together
	// This NPC moves towards OtherNPC
	ApplyKnockback(ToOther, EMFProximityKnockbackDistance, EMFProximityKnockbackDuration, OtherNPC->GetActorLocation(), true);

	// OtherNPC moves towards this NPC
	OtherNPC->ApplyKnockback(-ToOther, EMFProximityKnockbackDistance, EMFProximityKnockbackDuration, GetActorLocation(), true);

	// Damage is NOT applied here - it will be applied when capsules actually collide
	// via HandleNPCCollision() in UpdateKnockbackInterpolation()
}

void AShooterNPC::ForceResetCombatState()
{
	if (bIsDead)
	{
		return;
	}

	if (bIsCaptured)
	{
		ExitCapturedState();
	}

	if (bIsLaunched)
	{
		ExitLaunchedState();
	}

	if (bIsInKnockback)
	{
		// Temporarily clear bIsPermanentlyStunned so EndKnockbackStun actually runs
		const bool bWasPermanent = bIsPermanentlyStunned;
		bIsPermanentlyStunned = false;
		EndKnockbackStun();
		bIsPermanentlyStunned = bWasPermanent;
	}
}

void AShooterNPC::EndKnockbackStun()
{
	// Never exit stun if permanently stunned
	if (bIsPermanentlyStunned)
	{
		return;
	}

	// If captured, don't disrupt captured state — stun cleanup happens in ExitCapturedState
	if (bIsCaptured)
	{
		return;
	}

	// Clear knockback state
	UE_LOG(LogTemp, Warning, TEXT("[HP_DROP] %s EndKnockbackStun: CLEARING flags (bStunnedByExplosion=%d, bStunnedByNPCImpact=%d, bWasChannelingTarget=%d)"),
		*GetName(), bStunnedByExplosion, bStunnedByNPCImpact, bWasChannelingTarget);
	bIsInKnockback = false;
	bIsKnockbackInterpolating = false;
	bStunnedByExplosion = false;
	bStunnedByNPCImpact = false;
	bShouldStunOnNPCImpact = false;

	OnStunEnd.Broadcast(this);

	// Also clear launched state if active
	if (bIsLaunched)
	{
		bIsLaunched = false;
		if (ActiveLaunchedMontage)
		{
			if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
			{
				AnimInstance->Montage_Stop(0.2f, ActiveLaunchedMontage);
			}
			ActiveLaunchedMontage = nullptr;
		}
	}

	// Restore movement mode, ground friction, and rotation after knockback
	if (UCharacterMovementComponent* CharMovement = GetCharacterMovement())
	{
		CharMovement->SetMovementMode(MOVE_Walking);
		CharMovement->GroundFriction = CachedGroundFriction;
		CharMovement->Velocity = FVector::ZeroVector;
		CharMovement->bUseControllerDesiredRotation = bCachedUseControllerDesiredRotation;
	}

	// Restore character-level controller rotation
	bUseControllerRotationYaw = bCachedUseControllerRotationYaw;

	// Re-enable EMF forces
	if (bDisableEMFDuringKnockback && EMFVelocityModifier)
	{
		EMFVelocityModifier->SetEnabled(true);
	}

	// Restore focus on current target so NPC resumes facing the player
	if (AController* MyController = GetController())
	{
		if (AShooterAIController* AIController = Cast<AShooterAIController>(MyController))
		{
			if (AActor* Target = AIController->GetCurrentTarget())
			{
				AIController->SetFocus(Target);
			}
		}
	}

	// AI will resume pathfinding on next StateTree tick
}

// ==================== Captured State (Channeling Plate) ====================

void AShooterNPC::EnterCapturedState(UAnimMontage* OverrideMontage)
{
	if (bIsCaptured || bIsDead)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CAPTURE_DEBUG] %s EnterCapturedState BLOCKED! bIsCaptured=%d, bIsDead=%d"), *GetName(), bIsCaptured, bIsDead);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[CAPTURE_DEBUG] %s EnterCapturedState: bIsInKnockback=%d, bStunnedByExplosion=%d, MovementMode=%d"),
		*GetName(), bIsInKnockback, bStunnedByExplosion,
		GetCharacterMovement() ? (int32)GetCharacterMovement()->MovementMode.GetValue() : -1);

	bIsCaptured = true;
	bWasChannelingTarget = true; // Permanent flag for armor drop detection
	bIsInKnockback = true; // Blocks AI via StateTree IsInKnockback condition

	// If stunned, take over from stun state: clear stun timer, re-enable EMF for capture mechanics
	if (bStunnedByExplosion)
	{
		GetWorld()->GetTimerManager().ClearTimer(KnockbackStunTimer);
		if (bDisableEMFDuringKnockback && EMFVelocityModifier)
		{
			EMFVelocityModifier->SetEnabled(true);
		}
	}

	// Stop shooting immediately — burst fire cycle and permission retry won't check knockback on their own
	StopShooting();

	// Stop AI pathfinding
	if (AController* MyController = GetController())
	{
		if (AAIController* AIController = Cast<AAIController>(MyController))
		{
			if (UPathFollowingComponent* PathComp = AIController->GetPathFollowingComponent())
			{
				PathComp->AbortMove(*this, FPathFollowingResultFlags::UserAbort, FAIRequestID::CurrentRequest, EPathFollowingVelocityMode::Reset);
			}
			AIController->StopMovement();
		}
	}

	// Zero velocity and switch to Falling mode.
	// Must keep MOVE_Falling (not DisableMovement!) — viscous EMF capture forces need active CMC to pull NPC toward player.
	if (UCharacterMovementComponent* CharMovement = GetCharacterMovement())
	{
		CharMovement->StopMovementImmediately();
		CharMovement->SetMovementMode(MOVE_Falling);

		// Disable controller-driven rotation so NPC doesn't turn toward AI target during capture
		bCachedUseControllerDesiredRotation = CharMovement->bUseControllerDesiredRotation;
		CharMovement->bUseControllerDesiredRotation = false;

		UE_LOG(LogTemp, Warning, TEXT("[CAPTURE_DEBUG] %s movement set to MOVE_Falling, velocity zeroed, rotation disabled"), *GetName());
	}

	// Disable character-level controller rotation
	bCachedUseControllerRotationYaw = bUseControllerRotationYaw;
	bUseControllerRotationYaw = false;

	// Do NOT disable EMF — viscous capture needs it
	// Do NOT set knockback timer — capture duration managed externally

	// Play captured montage with seamless looping via NextSection
	UAnimMontage* MontageToPlay = OverrideMontage ? OverrideMontage : CapturedMontage.Get();
	if (MontageToPlay)
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			ActiveCapturedMontage = MontageToPlay;
			AnimInstance->Montage_Play(MontageToPlay, 1.0f);

			// Loop by pointing the last section back to the first — no callback, no gap, no blend
			const int32 NumSections = MontageToPlay->CompositeSections.Num();
			if (NumSections > 0)
			{
				const FName FirstSection = MontageToPlay->CompositeSections[0].SectionName;
				const FName LastSection = MontageToPlay->CompositeSections[NumSections - 1].SectionName;
				AnimInstance->Montage_SetNextSection(LastSection, FirstSection, MontageToPlay);
			}
		}
	}
}

void AShooterNPC::OnCapturedMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// Safety fallback — montage loops via SetNextSection, but if it somehow ends
	// (e.g. montage has no sections), restart it
	if (bIsCaptured && !bInterrupted && ActiveCapturedMontage)
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			AnimInstance->Montage_Play(ActiveCapturedMontage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f);

			const int32 NumSections = ActiveCapturedMontage->CompositeSections.Num();
			if (NumSections > 0)
			{
				const FName FirstSection = ActiveCapturedMontage->CompositeSections[0].SectionName;
				const FName LastSection = ActiveCapturedMontage->CompositeSections[NumSections - 1].SectionName;
				AnimInstance->Montage_SetNextSection(LastSection, FirstSection, ActiveCapturedMontage);
			}

			FOnMontageEnded EndDelegate;
			EndDelegate.BindUObject(this, &AShooterNPC::OnCapturedMontageEnded);
			AnimInstance->Montage_SetEndDelegate(EndDelegate, ActiveCapturedMontage);
		}
	}
}

void AShooterNPC::NotifyReverseLaunchRelease()
{
	if (bApplyReverseChannelingStun)
	{
		bShouldStunOnNPCImpact = true;
	}
}

void AShooterNPC::ExitCapturedState()
{
	if (!bIsCaptured)
	{
		return;
	}

	bIsCaptured = false;
	bIsInKnockback = false;

	// Reset capture-enabled-by-stun flag and restore bEnableViscousCapture
	if (bCaptureEnabledByStun)
	{
		bCaptureEnabledByStun = false;
		if (EMFVelocityModifier)
		{
			EMFVelocityModifier->bEnableViscousCapture = false;
		}
	}
	bStunnedByExplosion = false;

	// Fully restore movement, friction, rotation, and AI focus
	// (EnterCapturedState clears stun timer, so EndKnockbackStun never runs — we must do its job here)
	if (UCharacterMovementComponent* CharMovement = GetCharacterMovement())
	{
		CharMovement->SetMovementMode(MOVE_Walking);
		CharMovement->GroundFriction = CachedGroundFriction;
		CharMovement->Velocity = FVector::ZeroVector;
		CharMovement->bUseControllerDesiredRotation = bCachedUseControllerDesiredRotation;
	}
	bUseControllerRotationYaw = bCachedUseControllerRotationYaw;

	// Re-enable EMF forces (disabled by ApplyExplosionStun if stun was active before capture)
	if (bDisableEMFDuringKnockback && EMFVelocityModifier)
	{
		EMFVelocityModifier->SetEnabled(true);
	}

	// Restore AI focus so NPC resumes facing the player
	if (AController* MyController = GetController())
	{
		if (AShooterAIController* AIController = Cast<AShooterAIController>(MyController))
		{
			if (AActor* Target = AIController->GetCurrentTarget())
			{
				AIController->SetFocus(Target);
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[CAPTURE_DEBUG] %s ExitCapturedState: FULL restore (Walking, friction, rotation, EMF, AI focus)"), *GetName());

	// Stop captured montage
	if (ActiveCapturedMontage)
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			if (AnimInstance->Montage_IsPlaying(ActiveCapturedMontage))
			{
				AnimInstance->Montage_Stop(0.2f, ActiveCapturedMontage);
			}
		}
		ActiveCapturedMontage = nullptr;
	}

	// If NPC is moving fast enough, transition to launched state
	// (happens after reverse channeling flings NPC away)
	const float CurrentSpeed = GetVelocity().Size();
	if (CurrentSpeed >= LaunchedMinSpeed)
	{
		EnterLaunchedState();
		return;
	}

	// Even if speed is below LaunchedMinSpeed, the NPC was still flung by reverse channeling.
	// Set the stun-on-impact flag so any NPC-NPC collision applies stun.
	if (bApplyReverseChannelingStun && CurrentSpeed > KINDA_SMALL_NUMBER)
	{
		bShouldStunOnNPCImpact = true;
	}

	// === DEBUG ===
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Cyan,
			FString::Printf(TEXT("[RC STUN] ExitCapturedState %s (no launched): Speed=%.0f, bShouldStunOnNPCImpact=%d"),
				*GetName(), CurrentSpeed, bShouldStunOnNPCImpact));
	}

	// AI will resume on next StateTree tick
}

// ==================== Launched State ====================

void AShooterNPC::EnterLaunchedState()
{
	if (bIsLaunched || bIsDead)
	{
		return;
	}

	bIsLaunched = true;
	bIsInKnockback = true; // Keep AI blocked
	bShouldStunOnNPCImpact = bApplyReverseChannelingStun; // Persist stun intent beyond launched state

	// === DEBUG ===
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Cyan,
			FString::Printf(TEXT("[RC STUN] EnterLaunchedState %s: bShouldStunOnNPCImpact=%d (bApplyRCStun=%d)"),
				*GetName(), bShouldStunOnNPCImpact, bApplyReverseChannelingStun));
	}

	// Switch to launched force filtering weights
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetLaunchedForceFilteringActive(true);
	}

	// Play launched montage with seamless looping via NextSection
	UAnimMontage* MontageToPlay = LaunchedMontage.Get();
	if (MontageToPlay)
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			ActiveLaunchedMontage = MontageToPlay;
			AnimInstance->Montage_Play(MontageToPlay, 1.0f);

			const int32 NumSections = MontageToPlay->CompositeSections.Num();
			if (NumSections > 0)
			{
				const FName FirstSection = MontageToPlay->CompositeSections[0].SectionName;
				const FName LastSection = MontageToPlay->CompositeSections[NumSections - 1].SectionName;
				AnimInstance->Montage_SetNextSection(LastSection, FirstSection, MontageToPlay);
			}
		}
	}
}

void AShooterNPC::OnLaunchedMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// Safety fallback — montage loops via SetNextSection, but restart if it somehow ends
	if (bIsLaunched && !bInterrupted && ActiveLaunchedMontage)
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			AnimInstance->Montage_Play(ActiveLaunchedMontage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f);

			const int32 NumSections = ActiveLaunchedMontage->CompositeSections.Num();
			if (NumSections > 0)
			{
				const FName FirstSection = ActiveLaunchedMontage->CompositeSections[0].SectionName;
				const FName LastSection = ActiveLaunchedMontage->CompositeSections[NumSections - 1].SectionName;
				AnimInstance->Montage_SetNextSection(LastSection, FirstSection, ActiveLaunchedMontage);
			}

			FOnMontageEnded EndDelegate;
			EndDelegate.BindUObject(this, &AShooterNPC::OnLaunchedMontageEnded);
			AnimInstance->Montage_SetEndDelegate(EndDelegate, ActiveLaunchedMontage);
		}
	}
}

void AShooterNPC::ExitLaunchedState()
{
	if (!bIsLaunched)
	{
		return;
	}

	bIsLaunched = false;
	bIsInKnockback = false;

	// === DEBUG ===
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Orange,
			FString::Printf(TEXT("[RC STUN] ExitLaunchedState %s: bIsInKnockback now FALSE, bShouldStunOnNPCImpact=%d (should persist!)"),
				*GetName(), bShouldStunOnNPCImpact));
	}

	// Revert to normal force filtering weights
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetLaunchedForceFilteringActive(false);
	}

	// Stop launched montage
	if (ActiveLaunchedMontage)
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			if (AnimInstance->Montage_IsPlaying(ActiveLaunchedMontage))
			{
				AnimInstance->Montage_Stop(0.2f, ActiveLaunchedMontage);
			}
		}
		ActiveLaunchedMontage = nullptr;
	}
}

void AShooterNPC::UpdateLaunchedCollision()
{
	if (!bIsLaunched || bIsDead || !bEnableNPCCollision)
	{
		return;
	}

	// Use pre-collision velocity (stored before Super::Tick) for speed check
	const float PreCollisionSpeed = PreviousTickVelocity.Size();
	if (PreCollisionSpeed < LaunchedMinSpeed)
	{
		ExitLaunchedState();
		return;
	}

	// Sweep from previous position to current to catch fast-moving collisions.
	// CharacterMovement blocks capsule penetration, so static overlap won't detect
	// NPC-NPC hits — we need a sweep along the movement path.
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule || !GetWorld())
	{
		return;
	}

	const FVector CurrentPos = GetActorLocation();
	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();

	// Estimate previous position from pre-collision velocity
	const float DeltaTime = GetWorld()->GetDeltaSeconds();
	const FVector SweepStart = CurrentPos - PreviousTickVelocity * DeltaTime;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.bTraceComplex = false;

	TArray<FHitResult> Hits;
	bool bHasHits = GetWorld()->SweepMultiByChannel(
		Hits,
		SweepStart,
		CurrentPos,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeCapsule(CapsuleRadius * 1.1f, CapsuleHalfHeight),
		QueryParams
	);

	if (!bHasHits)
	{
		return;
	}

	for (const FHitResult& Hit : Hits)
	{
		AShooterNPC* OtherNPC = Cast<AShooterNPC>(Hit.GetActor());
		if (OtherNPC && !OtherNPC->IsDead())
		{
			if (PreCollisionSpeed >= NPCCollisionMinVelocity)
			{
				FVector CollisionPoint = Hit.ImpactPoint;
				HandleElasticNPCCollisionWithSpeed(OtherNPC, CollisionPoint, PreCollisionSpeed);
				ExitLaunchedState();
				return;
			}
		}
	}
}

// ==================== Coordinator Integration ====================

void AShooterNPC::RegisterWithCoordinator()
{
	if (!bUseCoordinator)
	{
		bHasAttackPermission = true; // Always allowed if not using coordinator
		return;
	}

	if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(this))
	{
		Coordinator->RegisterNPC(this);
	}
}

void AShooterNPC::UnregisterFromCoordinator()
{
	if (!bUseCoordinator)
	{
		return;
	}

	if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(this))
	{
		Coordinator->UnregisterNPC(this);
	}
}

bool AShooterNPC::RequestAttackPermission()
{
	// If external system (StateTree task) already obtained permission, skip coordinator check
	if (bExternalPermissionGranted || !bUseCoordinator)
	{
		bHasAttackPermission = true;
		return true;
	}

	if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(this))
	{
		bHasAttackPermission = Coordinator->RequestAttackPermission(this);
		return bHasAttackPermission;
	}

	// No coordinator found, allow attack
	bHasAttackPermission = true;
	return true;
}

void AShooterNPC::ReleaseAttackPermission()
{
	if (!bUseCoordinator || !bHasAttackPermission)
	{
		return;
	}

	if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(this))
	{
		Coordinator->NotifyAttackComplete(this);
	}

	bHasAttackPermission = false;
}

// ==================== Burst Fire ====================

void AShooterNPC::OnWeaponShotFired()
{
	// Only count shots if we're actively shooting
	if (!bIsShooting)
	{
		return;
	}

	CurrentBurstShots++;

	// Check if burst complete
	if (CurrentBurstShots >= BurstShotCount)
	{
		// Stop shooting and enter cooldown
		if (Weapon)
		{
			Weapon->StopFiring();
		}
		bInBurstCooldown = true;

		// Release attack permission during cooldown
		ReleaseAttackPermission();

		// Start cooldown timer
		GetWorld()->GetTimerManager().SetTimer(
			BurstCooldownTimer,
			this,
			&AShooterNPC::OnBurstCooldownEnd,
			BurstCooldown,
			false
		);
	}
}

void AShooterNPC::OnBurstCooldownEnd()
{
	bInBurstCooldown = false;
	CurrentBurstShots = 0;

	// If still want to shoot, try to get permission again
	if (bWantsToShoot && CurrentAimTarget.IsValid())
	{
		TryStartShooting();
	}
}

// ==================== Permission Retry ====================

void AShooterNPC::StartPermissionRetryTimer()
{
	// Don't start if already running
	if (GetWorld()->GetTimerManager().IsTimerActive(PermissionRetryTimer))
	{
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(
		PermissionRetryTimer,
		this,
		&AShooterNPC::TryStartShooting,
		PermissionRetryInterval,
		true  // Looping
	);
}

void AShooterNPC::StopPermissionRetryTimer()
{
	GetWorld()->GetTimerManager().ClearTimer(PermissionRetryTimer);
}

// ==================== Wall Slam ====================

void AShooterNPC::OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// Ignore if dead
	if (bIsDead)
	{
		return;
	}

	// Only apply wall slam damage during knockback
	// This prevents damage from normal movement collisions
	if (!bIsInKnockback)
	{
		return;
	}

	// NPC-NPC collision handling
	if (AShooterNPC* OtherNPC = Cast<AShooterNPC>(OtherActor))
	{
		// Handle NPC-NPC collision for ANY fast-moving knockback NPC.
		// Don't check bIsLaunched — it can be cleared by timing issues between
		// Tick order and deferred hit events. Use PreviousTickVelocity instead.
		if (!OtherNPC->IsDead())
		{
			const float ImpactSpeed = PreviousTickVelocity.Size();
			if (ImpactSpeed >= NPCCollisionMinVelocity)
			{
				FVector CollisionPoint = (GetActorLocation() + OtherNPC->GetActorLocation()) * 0.5f;
				HandleElasticNPCCollisionWithSpeed(OtherNPC, CollisionPoint, ImpactSpeed);
				if (bIsLaunched)
				{
					ExitLaunchedState();
				}
			}
		}
		return;
	}

	// Check cooldown to prevent multi-trigger on same impact
	float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastWallSlamTime < WallSlamCooldown)
	{
		return;
	}

	// Use the PRE-COLLISION velocity stored from Tick
	// GetVelocity() here returns post-collision velocity which is unreliable
	FVector ImpactVelocity = PreviousTickVelocity;
	float VelocityMagnitude = ImpactVelocity.Size();

	// Early exit if we weren't moving fast enough overall
	// This filters out standing/slow movement before doing the perpendicular calculation
	if (VelocityMagnitude < WallSlamVelocityThreshold * 0.5f)
	{
		return;
	}

	// Calculate velocity component perpendicular to the hit surface
	// This is how fast we were moving DIRECTLY INTO the surface (not along it)
	// Dot product of velocity with -normal gives perpendicular component
	// (Normal points away from surface, so we negate it to point into surface)
	float PerpendicularVelocity = FVector::DotProduct(ImpactVelocity, -Hit.ImpactNormal);

	// Only consider impacts where we were moving INTO the surface
	// Negative value means moving away from surface (shouldn't take damage)
	if (PerpendicularVelocity <= 0.0f)
	{
		return;
	}

#if WITH_EDITOR
	if (GEngine)
	{
		// Determine surface type for debug display
		FString SurfaceType = Hit.ImpactNormal.Z > 0.7f ? TEXT("FLOOR") :
		                      Hit.ImpactNormal.Z < -0.7f ? TEXT("CEILING") : TEXT("WALL");

		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow,
			FString::Printf(TEXT("Hit %s: PreVelocity=%.0f, PerpendicularVelocity=%.0f, Threshold=%.0f"),
				*SurfaceType, VelocityMagnitude, PerpendicularVelocity, WallSlamVelocityThreshold));
	}
#endif

	// Check if perpendicular velocity exceeds damage threshold
	if (PerpendicularVelocity < WallSlamVelocityThreshold)
	{
		return;
	}

	// Calculate damage based on perpendicular velocity above threshold
	float ExcessVelocity = PerpendicularVelocity - WallSlamVelocityThreshold;
	float WallSlamDamage = (ExcessVelocity / 100.0f) * WallSlamDamagePerVelocity;

	if (WallSlamDamage > 0.0f)
	{
		// Mark cooldown to prevent multi-trigger
		LastWallSlamTime = CurrentTime;

		// Apply damage to self with Wallslam damage type (Kinetic category)
		// DamageCauser = this (self-slam), used for health pickup drop logic (e.g. drone wall hit)
		FDamageEvent DamageEvent;
		DamageEvent.DamageTypeClass = UDamageType_Wallslam::StaticClass();
		TakeDamage(WallSlamDamage, DamageEvent, nullptr, this);

		// Play sound
		if (WallSlamSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, WallSlamSound, Hit.ImpactPoint);
		}

		// Spawn VFX - rotated so it lies flat on wall surface
		if (WallSlamVFX)
		{
			FRotator VFXRotation = UKismetMathLibrary::MakeRotFromYZ(Hit.ImpactNormal, FVector::UpVector);
			VFXRotation.Roll += 90.0f;  // Rotate to lie flat on wall

			// Offset slightly away from wall to prevent clipping
			FVector VFXLocation = Hit.ImpactPoint + Hit.ImpactNormal * 5.0f;

			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				GetWorld(),
				WallSlamVFX,
				VFXLocation,
				VFXRotation,
				FVector(WallSlamVFXScale),
				true,  // bAutoDestroy
				true,  // bAutoActivate
				ENCPoolMethod::None
			);
		}

#if WITH_EDITOR
		if (GEngine)
		{
			FString SurfaceType = Hit.ImpactNormal.Z > 0.7f ? TEXT("FLOOR (Fall Damage)") :
			                      Hit.ImpactNormal.Z < -0.7f ? TEXT("CEILING") : TEXT("WALL");

			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red,
				FString::Printf(TEXT("IMPACT DAMAGE on %s! Velocity=%.0f, Damage=%.1f"),
					*SurfaceType, PerpendicularVelocity, WallSlamDamage));
		}
#endif
	}
}

// ==================== AI Perception Handlers ====================

void AShooterNPC::OnEnemySpotted_Implementation(AActor* SpottedEnemy, FVector LastKnownLocation)
{
	// Base C++ implementation - can be overridden in Blueprint
	UE_LOG(LogTemp, Log, TEXT("[%s] OnEnemySpotted: %s at %s"),
		*GetName(),
		SpottedEnemy ? *SpottedEnemy->GetName() : TEXT("NULL"),
		*LastKnownLocation.ToString());
}

void AShooterNPC::OnEnemyLost_Implementation(AActor* LostEnemy)
{
	// Base C++ implementation - can be overridden in Blueprint
	UE_LOG(LogTemp, Log, TEXT("[%s] OnEnemyLost: %s"),
		*GetName(),
		LostEnemy ? *LostEnemy->GetName() : TEXT("NULL"));
}

void AShooterNPC::OnTeamPerceptionReceived_Implementation(AActor* ReportedEnemy, FVector LastKnownLocation)
{
	// Base C++ implementation - can be overridden in Blueprint
	UE_LOG(LogTemp, Log, TEXT("[%s] OnTeamPerceptionReceived: %s at %s"),
		*GetName(),
		ReportedEnemy ? *ReportedEnemy->GetName() : TEXT("NULL"),
		*LastKnownLocation.ToString());
}