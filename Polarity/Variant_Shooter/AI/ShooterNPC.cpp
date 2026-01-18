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
#include "../../AI/Components/AIAccuracyComponent.h"
#include "../../AI/Components/MeleeRetreatComponent.h"
#include "../../AI/Coordination/AICombatCoordinator.h"
#include "EMFVelocityModifier.h"
#include "EMF_FieldComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/DamageEvents.h"
#include "../DamageTypes/DamageType_Melee.h"
#include "NiagaraFunctionLibrary.h"

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

	// Register with combat coordinator
	RegisterWithCoordinator();

	// Bind capsule hit event for wall slam detection
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->OnComponentHit.AddDynamic(this, &AShooterNPC::OnCapsuleHit);
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

	// Broadcast charge update every tick
	OnChargeUpdated.Broadcast(ChargeValue, CurrentPolarity);

	// Check if polarity changed
	if (CurrentPolarity != PreviousPolarity)
	{
		OnPolarityChanged.Broadcast(CurrentPolarity, ChargeValue);
		PreviousPolarity = CurrentPolarity;
	}
}

float AShooterNPC::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// ignore if already dead
	if (bIsDead)
	{
		return 0.0f;
	}

	// Ignore friendly fire from other NPCs
	if (DamageCauser)
	{
		// Check if damage came from another ShooterNPC (through their weapon)
		AActor* DamageOwner = DamageCauser->GetOwner();
		if (Cast<AShooterNPC>(DamageCauser) || Cast<AShooterNPC>(DamageOwner))
		{
			return 0.0f;
		}

		// Also check the instigator's pawn
		if (EventInstigator)
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

		// fire the weapon
		Weapon->StartFiring();
		OnShotFired();
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

	// Disable EM field emission (dead bodies don't emit charge)
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetCharge(0.0f);
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

	// increment the team score
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->IncrementTeamScore(TeamByte);
	}

	// broadcast death event
	UE_LOG(LogTemp, Warning, TEXT("ShooterNPC::Die() - Broadcasting OnNPCDeath for %s"), *GetName());
	OnNPCDeath.Broadcast(this);

	// play death sound
	if (DeathSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, DeathSound, GetActorLocation());
	}

	// disable capsule collision
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// stop movement
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->StopActiveMovement();

	// enable ragdoll physics on the third person mesh
	GetMesh()->SetCollisionProfileName(RagdollCollisionProfile);
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetPhysicsBlendWeight(1.0f);

	// schedule actor destruction
	GetWorld()->GetTimerManager().SetTimer(DeathTimer, this, &AShooterNPC::DeferredDestruction, DeferredDestructionTime, false);
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

		Weapon->StartFiring();
		OnShotFired();
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
	Weapon->StopFiring();

	// Release attack permission
	ReleaseAttackPermission();
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

void AShooterNPC::ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration)
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

	// Disable EMF forces during knockback for consistent physics
	if (bDisableEMFDuringKnockback && EMFVelocityModifier)
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

	// Calculate interpolation alpha with easing (ease out for smooth deceleration)
	float Alpha = FMath::Clamp(KnockbackElapsedTime / KnockbackTotalDuration, 0.0f, 1.0f);
	float EasedAlpha = FMath::InterpEaseOut(0.0f, 1.0f, Alpha, 2.0f);

	// Calculate next position
	FVector CurrentPos = GetActorLocation();
	FVector NextPos = FMath::Lerp(KnockbackStartPosition, KnockbackTargetPosition, EasedAlpha);

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
	// Stop interpolation
	bIsKnockbackInterpolating = false;

	// Calculate impact velocity based on remaining distance and time
	float RemainingDistance = FVector::Dist(GetActorLocation(), KnockbackTargetPosition);
	float RemainingTime = KnockbackTotalDuration - KnockbackElapsedTime;
	float ImpactSpeed = (RemainingTime > 0.0f) ? RemainingDistance / RemainingTime : 0.0f;

	// Calculate perpendicular velocity component (how hard we hit the wall)
	float PerpendicularVelocity = ImpactSpeed * FMath::Abs(FVector::DotProduct(KnockbackDirection, -WallHit.ImpactNormal));

	// Move to wall position (slightly offset to avoid penetration)
	FVector WallPosition = WallHit.Location;
	SetActorLocation(WallPosition, false);

	// Stop all velocity
	if (UCharacterMovementComponent* CharMovement = GetCharacterMovement())
	{
		CharMovement->Velocity = FVector::ZeroVector;
	}

	// Check if impact is strong enough for wall slam damage
	if (PerpendicularVelocity >= WallSlamVelocityThreshold)
	{
		// Calculate damage based on perpendicular velocity above threshold
		float ExcessVelocity = PerpendicularVelocity - WallSlamVelocityThreshold;
		float WallSlamDamage = (ExcessVelocity / 100.0f) * WallSlamDamagePerVelocity;

		if (WallSlamDamage > 0.0f)
		{
			// Apply damage to self
			FDamageEvent DamageEvent;
			TakeDamage(WallSlamDamage, DamageEvent, nullptr, nullptr);

			// Play sound
			if (WallSlamSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, WallSlamSound, WallHit.ImpactPoint);
			}

			// Spawn VFX
			if (WallSlamVFX)
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(
					GetWorld(),
					WallSlamVFX,
					WallHit.ImpactPoint,
					WallHit.ImpactNormal.Rotation(),
					FVector(WallSlamVFXScale),
					true,  // bAutoDestroy
					true,  // bAutoActivate
					ENCPoolMethod::None
				);
			}

#if WITH_EDITOR
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red,
					FString::Printf(TEXT("WALL SLAM during knockback! ImpactSpeed=%.0f, Damage=%.1f"),
						PerpendicularVelocity, WallSlamDamage));
			}
#endif
		}
	}
	else
	{
#if WITH_EDITOR
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow,
				FString::Printf(TEXT("Hit wall during knockback (no damage): ImpactSpeed=%.0f, Threshold=%.0f"),
					PerpendicularVelocity, WallSlamVelocityThreshold));
		}
#endif
	}
}

void AShooterNPC::EndKnockbackStun()
{
	// Clear knockback state
	bIsInKnockback = false;
	bIsKnockbackInterpolating = false;

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

void AShooterNPC::OnShotFired()
{
	CurrentBurstShots++;

	// Check if burst complete
	if (CurrentBurstShots >= BurstShotCount)
	{
		// Stop shooting and enter cooldown
		Weapon->StopFiring();
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

		// Apply damage to self
		FDamageEvent DamageEvent;
		TakeDamage(WallSlamDamage, DamageEvent, nullptr, nullptr);

		// Play sound
		if (WallSlamSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, WallSlamSound, Hit.ImpactPoint);
		}

		// Spawn VFX
		if (WallSlamVFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				GetWorld(),
				WallSlamVFX,
				Hit.ImpactPoint,
				Hit.ImpactNormal.Rotation(),
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