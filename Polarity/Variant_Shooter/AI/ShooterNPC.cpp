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
#include "../../AI/Components/AIAccuracyComponent.h"
#include "../../AI/Components/MeleeRetreatComponent.h"
#include "../../AI/Coordination/AICombatCoordinator.h"
#include "EMFVelocityModifier.h"
#include "EMF_FieldComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "../DamageTypes/DamageType_Melee.h"
#include "../DamageTypes/DamageType_Wallslam.h"
#include "../DamageTypes/DamageType_EMFProximity.h"
#include "../UI/DamageNumbersSubsystem.h"
#include "NiagaraFunctionLibrary.h"
#include "Polarity/Checkpoint/CheckpointSubsystem.h"
#include "Boss/BossProjectile.h"

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
}

void AShooterNPC::BeginPlay()
{
	Super::BeginPlay();

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
}

void AShooterNPC::Tick(float DeltaTime)
{
	// Store velocity BEFORE Super::Tick processes any movement/collisions
	// This gives us the "pre-collision" velocity for impact damage calculation
	PreviousTickVelocity = GetVelocity();

	Super::Tick(DeltaTime);

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

	// Update Charge/Polarity - get charge from EMFVelocityModifier
	float ChargeValue = 0.0f;
	if (EMFVelocityModifier)
	{
		ChargeValue = EMFVelocityModifier->GetCharge();
	}

	// Sync FieldComponent with EMFVelocityModifier charge
	if (FieldComponent)
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
			// Check if damage came from another ShooterNPC (through their weapon)
			AActor* DamageOwner = DamageCauser->GetOwner();
			if (Cast<AShooterNPC>(DamageCauser) || Cast<AShooterNPC>(DamageOwner))
			{
				return 0.0f;
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
	// Don't continue if dead
	if (bIsDead)
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

	// play death sound
	if (DeathSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, DeathSound, GetActorLocation());
	}

	// ============== AGGRESSIVE DEACTIVATION FOR PERFORMANCE ==============

	// Disable ALL collision immediately
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Stop movement completely
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
		MoveComp->SetComponentTickEnabled(false);
	}

	// Hide mesh instead of ragdoll (ragdoll is expensive)
	GetMesh()->SetVisibility(false);
	GetMesh()->SetComponentTickEnabled(false);

	// Disable EMF components and unregister from registry immediately
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetCharge(0.0f);
		EMFVelocityModifier->SetComponentTickEnabled(false);
	}
	if (FieldComponent)
	{
		FieldComponent->UnregisterFromRegistry();  // Immediately remove from EMF calculations
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

	// Unpossess to stop AI controller
	if (AController* MyController = GetController())
	{
		MyController->UnPossess();
	}

	// Disable actor tick
	SetActorTickEnabled(false);

	// Destroy weapon to free resources
	if (Weapon)
	{
		Weapon->Destroy();
		Weapon = nullptr;
	}

	// Schedule fast destruction (VFX should be detached by now)
	GetWorld()->GetTimerManager().SetTimer(DeathTimer, this, &AShooterNPC::DeferredDestruction, 0.5f, false);
}

void AShooterNPC::DeferredDestruction()
{
	Destroy();
}

void AShooterNPC::StartShooting(AActor* ActorToShoot, bool bHasExternalPermission)
{
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
	// Don't shoot if dead
	if (bIsDead)
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
	const FVector Start = GetActorLocation() + FVector(0.0f, 0.0f, 50.0f); // Offset up from feet
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

void AShooterNPC::ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation, bool bKeepEMFEnabled)
{
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

	// Store knockback parameters
	KnockbackStartPosition = GetActorLocation();
	KnockbackDirection = InKnockbackDirection.GetSafeNormal();
	KnockbackTargetPosition = KnockbackStartPosition + KnockbackDirection * FinalDistance;
	KnockbackTotalDuration = Duration;
	KnockbackElapsedTime = 0.0f;
	KnockbackAttackerPosition = AttackerLocation;

	// Stop AI pathfinding completely
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
	}

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
			TakeDamage(WallSlamDamage, DamageEvent, nullptr, nullptr);

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

	UE_LOG(LogTemp, Warning, TEXT("[NPC Collision] Applying knockback to SELF %s: Dir=(%.1f,%.1f,%.1f), Dist=%.0f, Dur=%.2f"),
		*GetName(), -CollisionDirection.X, -CollisionDirection.Y, -CollisionDirection.Z,
		KnockbackDistance, KnockbackDuration);

	// Apply knockback to myself (backwards)
	ApplyKnockback(-CollisionDirection, KnockbackDistance, KnockbackDuration, OtherNPC->GetActorLocation());

	UE_LOG(LogTemp, Warning, TEXT("[NPC Collision] Applying knockback to OTHER NPC %s: Dir=(%.1f,%.1f,%.1f), Dist=%.0f, Dur=%.2f"),
		*OtherNPC->GetName(), CollisionDirection.X, CollisionDirection.Y, CollisionDirection.Z,
		KnockbackDistance, KnockbackDuration);

	// Apply knockback to other NPC (forwards)
	OtherNPC->ApplyKnockback(CollisionDirection, KnockbackDistance, KnockbackDuration, GetActorLocation());

	// Apply damage to both NPCs with a short delay
	// This allows the knockback impulse to be applied first, making deaths look better
	// Pass nullptr as DamageCauser to bypass friendly fire check (NPC-NPC collision damage is intentional)
	// Split damage by type: Kinetic (Wallslam) and EMF (EMFProximity)
	if (CollisionDamage > 0.0f)
	{
		const float DamageDelay = 0.1f;
		float KineticDamage = KineticCollisionDamage;
		float EMFDamage = EMFCollisionDamage;

		// Delay damage to self
		FTimerHandle SelfDamageTimer;
		GetWorld()->GetTimerManager().SetTimer(
			SelfDamageTimer,
			[this, KineticDamage, EMFDamage]()
			{
				if (!bIsDead)
				{
					// Apply Kinetic damage (Wallslam type)
					if (KineticDamage > 0.0f)
					{
						FDamageEvent KineticEvent;
						KineticEvent.DamageTypeClass = UDamageType_Wallslam::StaticClass();
						TakeDamage(KineticDamage, KineticEvent, nullptr, nullptr);
					}
					// Apply EMF damage (EMFProximity type)
					if (EMFDamage > 0.0f)
					{
						FDamageEvent EMFEvent;
						EMFEvent.DamageTypeClass = UDamageType_EMFProximity::StaticClass();
						TakeDamage(EMFDamage, EMFEvent, nullptr, nullptr);
					}
				}
			},
			DamageDelay,
			false
		);

		// Delay damage to other NPC
		FTimerHandle OtherDamageTimer;
		GetWorld()->GetTimerManager().SetTimer(
			OtherDamageTimer,
			[OtherNPC, KineticDamage, EMFDamage]()
			{
				if (OtherNPC && !OtherNPC->bIsDead)
				{
					// Apply Kinetic damage (Wallslam type)
					if (KineticDamage > 0.0f)
					{
						FDamageEvent KineticEvent;
						KineticEvent.DamageTypeClass = UDamageType_Wallslam::StaticClass();
						OtherNPC->TakeDamage(KineticDamage, KineticEvent, nullptr, nullptr);
					}
					// Apply EMF damage (EMFProximity type)
					if (EMFDamage > 0.0f)
					{
						FDamageEvent EMFEvent;
						EMFEvent.DamageTypeClass = UDamageType_EMFProximity::StaticClass();
						OtherNPC->TakeDamage(EMFDamage, EMFEvent, nullptr, nullptr);
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

void AShooterNPC::EndKnockbackStun()
{
	// Clear knockback state
	bIsInKnockback = false;
	bIsKnockbackInterpolating = false;

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

	// Restore original ground friction after knockback slide
	if (UCharacterMovementComponent* CharMovement = GetCharacterMovement())
	{
		CharMovement->GroundFriction = CachedGroundFriction;
		CharMovement->Velocity = FVector::ZeroVector;
	}

	// Re-enable EMF forces
	if (bDisableEMFDuringKnockback && EMFVelocityModifier)
	{
		EMFVelocityModifier->SetEnabled(true);
	}

	// AI will resume pathfinding on next StateTree tick
}

// ==================== Captured State (Channeling Plate) ====================

void AShooterNPC::EnterCapturedState(UAnimMontage* OverrideMontage)
{
	if (bIsCaptured || bIsDead)
	{
		return;
	}

	bIsCaptured = true;
	bIsInKnockback = true; // Blocks AI via StateTree IsInKnockback condition

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

	// Do NOT zero velocity — viscosity manages it
	// Do NOT disable EMF — viscous capture needs it
	// Do NOT set knockback timer — capture duration managed externally

	// Play captured montage (will loop via OnCapturedMontageEnded callback)
	UAnimMontage* MontageToPlay = OverrideMontage ? OverrideMontage : CapturedMontage.Get();
	if (MontageToPlay)
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			ActiveCapturedMontage = MontageToPlay;
			AnimInstance->Montage_Play(MontageToPlay, 1.0f);

			FOnMontageEnded EndDelegate;
			EndDelegate.BindUObject(this, &AShooterNPC::OnCapturedMontageEnded);
			AnimInstance->Montage_SetEndDelegate(EndDelegate, MontageToPlay);
		}
	}
}

void AShooterNPC::OnCapturedMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// If still captured and not interrupted — loop the montage
	if (bIsCaptured && !bInterrupted && ActiveCapturedMontage)
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			AnimInstance->Montage_Play(ActiveCapturedMontage, 1.0f);

			FOnMontageEnded EndDelegate;
			EndDelegate.BindUObject(this, &AShooterNPC::OnCapturedMontageEnded);
			AnimInstance->Montage_SetEndDelegate(EndDelegate, ActiveCapturedMontage);
		}
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

	// Restore friction
	if (UCharacterMovementComponent* CharMovement = GetCharacterMovement())
	{
		CharMovement->GroundFriction = CachedGroundFriction;
	}

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

	// Play launched montage (looping via callback)
	UAnimMontage* MontageToPlay = LaunchedMontage.Get();
	if (MontageToPlay)
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			ActiveLaunchedMontage = MontageToPlay;
			AnimInstance->Montage_Play(MontageToPlay, 1.0f);

			FOnMontageEnded EndDelegate;
			EndDelegate.BindUObject(this, &AShooterNPC::OnLaunchedMontageEnded);
			AnimInstance->Montage_SetEndDelegate(EndDelegate, MontageToPlay);
		}
	}
}

void AShooterNPC::OnLaunchedMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (bIsLaunched && !bInterrupted && ActiveLaunchedMontage)
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			AnimInstance->Montage_Play(ActiveLaunchedMontage, 1.0f);

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
	if (!bUseCoordinator)
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
		FDamageEvent DamageEvent;
		DamageEvent.DamageTypeClass = UDamageType_Wallslam::StaticClass();
		TakeDamage(WallSlamDamage, DamageEvent, nullptr, nullptr);

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