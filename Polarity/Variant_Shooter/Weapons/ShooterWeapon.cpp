// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterWeapon.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "ShooterProjectile.h"
#include "EMFProjectile.h"
#include "ProjectilePoolSubsystem.h"
#include "ShooterWeaponHolder.h"
#include "EMF_FieldComponent.h"
#include "EMFVelocityModifier.h"
#include "Components/SceneComponent.h"
#include "Camera/CameraComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/DamageEvents.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "DrawDebugHelpers.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundAttenuation.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/ShooterDummy.h"
#include "EMFPhysicsProp.h"

namespace
{
	/** Check if actor is dead after TakeDamage (synchronous check via HP/bIsDead flags) */
	bool IsActorDeadAfterDamage(AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return true;
		}

		// ShooterNPC covers ShooterNPC, FlyingDrone, MeleeNPC, BossCharacter
		if (AShooterNPC* NPC = Cast<AShooterNPC>(Actor))
		{
			return NPC->IsDead();
		}

		// Player character
		if (AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(Actor))
		{
			return ShooterChar->IsDead();
		}

		// Training dummies
		if (AShooterDummy* Dummy = Cast<AShooterDummy>(Actor))
		{
			return Dummy->IsDead();
		}

		// Physics props
		if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(Actor))
		{
			return Prop->IsDead();
		}

		// Fallback for unknown actor types
		return Actor->IsPendingKillPending();
	}
}

AShooterWeapon::AShooterWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	// create the root
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// create the first person mesh
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(RootComponent);

	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	FirstPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonMesh->bOnlyOwnerSee = true;

	// create the third person mesh
	ThirdPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Third Person Mesh"));
	ThirdPersonMesh->SetupAttachment(RootComponent);

	ThirdPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	ThirdPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	ThirdPersonMesh->bOwnerNoSee = true;

	// Create ADS camera component on the first person mesh
	// It will be attached to the Sight socket in BeginPlay after meshes are set up
	ADSCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("ADS Camera"));
	ADSCameraComponent->SetupAttachment(FirstPersonMesh);
}

void AShooterWeapon::BeginPlay()
{
	Super::BeginPlay();

	// subscribe to the owner's destroyed delegate
	GetOwner()->OnDestroyed.AddDynamic(this, &AShooterWeapon::OnOwnerDestroyed);

	// cast the weapon owner
	WeaponOwner = Cast<IShooterWeaponHolder>(GetOwner());
	PawnOwner = Cast<APawn>(GetOwner());

	// Cache movement component for Heat System speed calculations
	if (ACharacter* CharOwner = Cast<ACharacter>(GetOwner()))
	{
		CachedMovementComponent = CharOwner->GetCharacterMovement();
	}

	// NPC optimization: hide first person mesh for non-player owners
	if (!PawnOwner || !PawnOwner->IsPlayerControlled())
	{
		if (FirstPersonMesh)
		{
			FirstPersonMesh->SetVisibility(false);
			FirstPersonMesh->SetComponentTickEnabled(false);
		}
	}

	// fill the first ammo clip
	CurrentBullets = MagazineSize;

	// attach the meshes to the owner
	WeaponOwner->AttachWeaponMeshes(this);

	// Attach ADS camera to sight socket on the weapon's first person mesh
	if (ADSCameraComponent && FirstPersonMesh && FirstPersonMesh->DoesSocketExist(ADSSocketName))
	{
		ADSCameraComponent->AttachToComponent(FirstPersonMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, ADSSocketName);
	}
}

void AShooterWeapon::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

void AShooterWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Update Heat System
	if (bUseHeatSystem)
	{
		UpdateHeat(DeltaTime);
	}
}

void AShooterWeapon::OnOwnerDestroyed(AActor* DestroyedActor)
{
	// ensure this weapon is destroyed when the owner is destroyed
	Destroy();
}

void AShooterWeapon::ActivateWeapon()
{
	// unhide this weapon
	SetActorHiddenInGame(false);

	// notify the owner
	WeaponOwner->OnWeaponActivated(this);
}

void AShooterWeapon::DeactivateWeapon()
{
	// ensure we're no longer firing this weapon while deactivated
	StopFiring();

	// hide the weapon
	SetActorHiddenInGame(true);

	// notify the owner
	WeaponOwner->OnWeaponDeactivated(this);
}

void AShooterWeapon::StartFiring()
{
	// raise the firing flag
	bIsFiring = true;

	// check how much time has passed since we last shot
	// this may be under the refire rate if the weapon shoots slow enough and the player is spamming the trigger
	const float TimeSinceLastShot = GetWorld()->GetTimeSeconds() - TimeOfLastShot;
	const float CurrentRefireRate = GetCurrentRefireRate();

	if (TimeSinceLastShot > CurrentRefireRate)
	{
		// fire the weapon right away
		Fire();

	}
	else {

		// if we're full auto, schedule the next shot
		if (bFullAuto)
		{
			GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, TimeSinceLastShot, false);
		}

	}
}

void AShooterWeapon::StopFiring()
{
	// lower the firing flag
	bIsFiring = false;

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

void AShooterWeapon::Fire()
{
	// ensure the player still wants to fire. They may have let go of the trigger
	if (!bIsFiring)
	{
		return;
	}

	// Check charge requirements if enabled
	float ChargeMultiplier = 1.0f;
	if (bUseChargeFiring)
	{
		if (!TryConsumeCharge(ChargeMultiplier))
		{
			// Not enough charge - stop firing
			StopFiring();
			return;
		}
	}

	// Spawn muzzle flash effect for all weapon types
	SpawnMuzzleFlashEffect();

	// Play fire sound with random pitch
	PlayFireSound();

	// Add heat from firing
	if (bUseHeatSystem)
	{
		AddHeat(HeatPerShot);
	}

	// Get target location
	const FVector TargetLocation = WeaponOwner->GetWeaponTargetLocation();

	// Fire based on mode
	if (bUseHitscan)
	{
		FireHitscan(TargetLocation);
	}
	else
	{
		FireProjectile(TargetLocation, ChargeMultiplier);
	}

	// update the time of our last shot
	TimeOfLastShot = GetWorld()->GetTimeSeconds();

	// Notify listeners that a shot was fired (for NPC burst counting)
	OnShotFired.Broadcast();

	// make noise so the AI perception system can hear us
	MakeNoise(ShotLoudness, PawnOwner, PawnOwner->GetActorLocation(), ShotNoiseRange, ShotNoiseTag);

	// are we full auto?
	// Use current refire rate which factors in heat
	const float ActualRefireRate = GetCurrentRefireRate();

	if (bFullAuto)
	{
		// schedule the next shot
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, ActualRefireRate, false);
	}
	else {

		// for semi-auto weapons, schedule the cooldown notification
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::FireCooldownExpired, ActualRefireRate, false);

	}
}

void AShooterWeapon::FireCooldownExpired()
{
	// notify the owner
	WeaponOwner->OnSemiWeaponRefire();
}

void AShooterWeapon::FireProjectile(const FVector& TargetLocation, float ChargeMultiplier)
{
	// get the projectile transform
	FTransform ProjectileTransform = CalculateProjectileSpawnTransform(TargetLocation);

	// Get projectile from pool (or spawn new if pool empty)
	AShooterProjectile* Projectile = nullptr;
	if (UProjectilePoolSubsystem* Pool = GetWorld()->GetSubsystem<UProjectilePoolSubsystem>())
	{
		Projectile = Pool->GetProjectile(ProjectileClass, ProjectileTransform, GetOwner(), PawnOwner);
	}
	else
	{
		// Fallback to direct spawn if pool subsystem not available
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
		SpawnParams.Owner = GetOwner();
		SpawnParams.Instigator = PawnOwner;
		Projectile = GetWorld()->SpawnActor<AShooterProjectile>(ProjectileClass, ProjectileTransform, SpawnParams);
	}

	// If charge-based firing, scale projectile charge and match player polarity
	if (bUseChargeFiring && Projectile)
	{
		if (AEMFProjectile* EMFProj = Cast<AEMFProjectile>(Projectile))
		{
			// Get player's charge sign
			AActor* WeaponOwnerActor = GetOwner();
			if (WeaponOwnerActor)
			{
				UEMFVelocityModifier* EMFMod = WeaponOwnerActor->FindComponentByClass<UEMFVelocityModifier>();
				if (EMFMod)
				{
					float PlayerCharge = EMFMod->GetCharge();
					float PlayerSign = FMath::Sign(PlayerCharge);

					// Set projectile charge with same sign as player
					float BaseCharge = FMath::Abs(EMFProj->GetProjectileCharge());
					EMFProj->SetProjectileCharge(PlayerSign * BaseCharge * ChargeMultiplier);

					UE_LOG(LogTemp, Log, TEXT("ShooterWeapon: Projectile charge set to %.2f (player sign: %.0f, multiplier: %.2f)"),
						PlayerSign * BaseCharge * ChargeMultiplier, PlayerSign, ChargeMultiplier);
				}
			}
		}
	}

	// play the firing montage
	WeaponOwner->PlayFiringMontage(FiringMontage);

	// add recoil
	WeaponOwner->AddWeaponRecoil(FiringRecoil);

	// consume bullets
	--CurrentBullets;

	// if the clip is depleted, reload it
	if (CurrentBullets <= 0)
	{
		CurrentBullets = MagazineSize;
	}

	// update the weapon HUD
	WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
}

FTransform AShooterWeapon::CalculateProjectileSpawnTransform(const FVector& TargetLocation) const
{
	// Use ThirdPersonMesh for NPCs, FirstPersonMesh for players
	USkeletalMeshComponent* MuzzleMesh = (PawnOwner && PawnOwner->IsPlayerControlled()) ? FirstPersonMesh : ThirdPersonMesh;

	// find the muzzle location
	const FVector MuzzleLoc = MuzzleMesh->GetSocketLocation(MuzzleSocketName);

	// calculate the spawn location ahead of the muzzle
	const FVector SpawnLoc = MuzzleLoc + ((TargetLocation - MuzzleLoc).GetSafeNormal() * MuzzleOffset);

	// find the aim rotation vector while applying some variance to the target
	const FRotator AimRot = UKismetMathLibrary::FindLookAtRotation(SpawnLoc, TargetLocation + (UKismetMathLibrary::RandomUnitVector() * AimVariance));

	// return the built transform
	return FTransform(AimRot, SpawnLoc, FVector::OneVector);
}

// ==================== Hitscan Implementation ====================

void AShooterWeapon::FireHitscan(const FVector& TargetLocation)
{
	// Use ThirdPersonMesh for NPCs, FirstPersonMesh for players
	USkeletalMeshComponent* MuzzleMesh = (PawnOwner && PawnOwner->IsPlayerControlled()) ? FirstPersonMesh : ThirdPersonMesh;
	const FVector MuzzleLocation = MuzzleMesh->GetSocketLocation(MuzzleSocketName);

	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°)
	FVector ViewDir = FVector::ForwardVector;
	FVector ViewLocation = MuzzleLocation; // Fallback

	if (PawnOwner)
	{
		ViewDir = PawnOwner->GetBaseAimRotation().Vector();
		ViewLocation = PawnOwner->GetPawnViewLocation();
	}

	// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸, ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°)
	FVector ToTargetVector = TargetLocation - MuzzleLocation;
	float DistanceToTarget = ToTargetVector.Size();
	FVector ToTargetDir = ToTargetVector.GetSafeNormal();

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	// 1.0 = ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾, 0.0 = 90 ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â², -1.0 = ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´
	float DotP = FVector::DotProduct(ToTargetDir, ViewDir);

	// === ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â§ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ ===

	// 1. ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¯ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â (ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â) - ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â· ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
	//DrawDebugLine(GetWorld(), ViewLocation, TargetLocation, FColor::Green, false, 3.0f, 0, 1.0f);

	// 2. ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ "ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾" ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â«ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â (ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â) - ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
	//DrawDebugLine(GetWorld(), MuzzleLocation, TargetLocation, FColor::Red, false, 3.0f, 0, 1.0f);

	// 3. ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°), ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢, ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡
	//DrawDebugSphere(GetWorld(), MuzzleLocation, 10.0f, 12, FColor::Blue, false, 3.0f);

	// 4. ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½
	FString DebugMsg = FString::Printf(TEXT("Dist: %.1f | Dot: %.3f | Fix Applied: %s"),
		DistanceToTarget,
		DotP,
		(DistanceToTarget < 100.0f || DotP < 0.5f) ? TEXT("YES") : TEXT("NO"));

	//GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow, DebugMsg);

	// === ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¦ ===

	FVector Direction;

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â£ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸:
	// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ 100 ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ (1 ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬) ÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ (< 0.5 ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ 60 ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²)
	if (DistanceToTarget < 100.0f || DotP < 0.5f)
	{
		// FIX: ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹
		Direction = ViewDir;
		//UE_LOG(LogTemp, Warning, TEXT("FireHitscan: FIXED direction used (Too close or bad angle)"));
	}
	else
	{
		// STANDARD: ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âº ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		Direction = ToTargetDir;
	}

	// === ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¤ÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ===
	// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ 2 ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	//DrawDebugLine(GetWorld(), MuzzleLocation, MuzzleLocation + (Direction * 200.0f), FColor::Yellow, false, 3.0f, 0, 2.0f);


	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â (AimVariance)
	if (AimVariance > 0.0f)
	{
		FVector Variance = FMath::VRand() * FMath::Tan(FMath::DegreesToRadians(AimVariance));
		Direction = (Direction + Variance).GetSafeNormal();
	}

	// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»
	PerformHitscan(MuzzleLocation, Direction, 1.0f, 0);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	WeaponOwner->PlayFiringMontage(FiringMontage);
	WeaponOwner->AddWeaponRecoil(FiringRecoil);

	--CurrentBullets;
	if (CurrentBullets <= 0)
	{
		CurrentBullets = MagazineSize;
	}

	WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
}

// ========================================
// DEBUG: ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
// #define DEBUG_CONE_HITSCAN 1
// ========================================

void AShooterWeapon::PerformHitscan(const FVector& Start, const FVector& Direction, float RemainingEnergy, int32 ReflectionCount)
{
	float SegmentMaxDistance = MaxHitscanRange * RemainingEnergy;
	FVector End = Start + Direction * SegmentMaxDistance;

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦)
	float DivergenceAngle = WaveDivergence * MaxDivergenceAngle;
	float ConeHalfAngleRad = FMath::DegreesToRadians(DivergenceAngle);
	float CosHalfAngle = FMath::Cos(ConeHalfAngleRad);

	// ===== ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¨ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ 1: Line trace ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹) =====
	FHitResult WallHitResult;
	FCollisionQueryParams WallQueryParams;
	WallQueryParams.AddIgnoredActor(this);
	WallQueryParams.AddIgnoredActor(GetOwner());
	WallQueryParams.bReturnPhysicalMaterial = true;

	// Используем ECC_Visibility channel вместо ObjectType - 
	// он корректно учитывает collision responses и игнорирует triggers/overlaps
	bool bHitWall = GetWorld()->LineTraceSingleByChannel(
		WallHitResult,
		Start,
		End,
		ECC_Visibility,
		WallQueryParams
	);

	float MaxDistance = bHitWall ? WallHitResult.Distance : SegmentMaxDistance;
	FVector BeamEnd = bHitWall ? WallHitResult.ImpactPoint : End;

	// DEBUG: Log what the Visibility line trace hit (helps diagnose EMFPhysicsProp hits)
	if (bHitWall && WallHitResult.GetActor())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Hitscan DEBUG] Visibility trace hit: %s (Class: %s) at dist=%.0f"),
			*WallHitResult.GetActor()->GetName(),
			*WallHitResult.GetActor()->GetClass()->GetName(),
			WallHitResult.Distance);
	}

	// Direct damage to non-Pawn physics actors hit by Visibility trace (e.g. EMFPhysicsProp).
	// The cone sweep only queries ECC_Pawn, so PhysicsActor objects are invisible to it.
	// Apply damage via the existing ApplyHitscanDamage path for these actors.
	if (bHitWall && WallHitResult.GetActor() && !Cast<APawn>(WallHitResult.GetActor()))
	{
		AActor* WallActor = WallHitResult.GetActor();
		if (WallActor->CanBeDamaged())
		{
			ApplyHitscanDamage(WallHitResult, RemainingEnergy, WallHitResult.Distance, 0.0f);
		}
	}

#if DEBUG_CONE_HITSCAN
	// ===== DEBUG: ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° =====
	const float DebugDuration = 2.0f;
	const bool bPersistent = false;

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â)
	DrawDebugLine(GetWorld(), Start, BeamEnd, FColor::Green, bPersistent, DebugDuration, 0, 2.0f);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°)
	DrawDebugSphere(GetWorld(), Start, 5.0f, 8, FColor::Blue, bPersistent, DebugDuration);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°, ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦)
	DrawDebugSphere(GetWorld(), BeamEnd, 10.0f, 8, bHitWall ? FColor::Red : FColor::Green, bPersistent, DebugDuration);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
	const int32 NumConeLines = 16; // ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	FVector Right = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		Right = FVector::CrossProduct(Direction, FVector::RightVector).GetSafeNormal();
	}
	FVector Up = FVector::CrossProduct(Right, Direction).GetSafeNormal();

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦
	TArray<float> DebugDistances = { 100.0f, 500.0f, 1000.0f, MaxDistance * 0.5f, MaxDistance };

	for (float DebugDist : DebugDistances)
	{
		if (DebugDist > MaxDistance) continue;

		float ConeRadius = CalculateWaveRadius(DebugDist);
		FVector ConeCenter = Start + Direction * DebugDist;

		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		FVector PrevPoint = ConeCenter + Right * ConeRadius;
		for (int32 i = 1; i <= NumConeLines; i++)
		{
			float Angle = (float)i / (float)NumConeLines * 2.0f * PI;
			FVector PointOnCircle = ConeCenter + (Right * FMath::Cos(Angle) + Up * FMath::Sin(Angle)) * ConeRadius;

			// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â)
			DrawDebugLine(GetWorld(), PrevPoint, PointOnCircle, FColor::Yellow, bPersistent, DebugDuration, 0, 1.0f);

			PrevPoint = PointOnCircle;
		}

		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âº ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ 4-ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½)
		for (int32 i = 0; i < NumConeLines; i += 4)
		{
			float Angle = (float)i / (float)NumConeLines * 2.0f * PI;
			FVector PointOnCircle = ConeCenter + (Right * FMath::Cos(Angle) + Up * FMath::Sin(Angle)) * ConeRadius;

			// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ)
			DrawDebugLine(GetWorld(), Start, PointOnCircle, FColor::Orange, bPersistent, DebugDuration, 0, 0.5f);
		}

		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ (wireframe)
		DrawDebugCircle(GetWorld(), ConeCenter, ConeRadius, 32, FColor::Cyan, bPersistent, DebugDuration, 0, 1.0f, Up, Right, false);
	}
	// ===== END DEBUG =====
#endif // DEBUG_CONE_HITSCAN

	// ===== ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¨ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ 2: Multi Sweep ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¥ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° =====
	// ÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	TArray<FHitResult> SweepHits;
	TArray<AActor*> HitTargets;

	FCollisionQueryParams SweepQueryParams;
	SweepQueryParams.AddIgnoredActor(this);
	SweepQueryParams.AddIgnoredActor(GetOwner());
	SweepQueryParams.bReturnPhysicalMaterial = true;

	FCollisionObjectQueryParams PawnObjectParams;
	PawnObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â sweep = ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
	float MaxConeRadius = CalculateWaveRadius(MaxDistance);

	// Multi sweep ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â
	GetWorld()->SweepMultiByObjectType(
		SweepHits,
		Start,
		BeamEnd,
		FQuat::Identity,
		PawnObjectParams,
		FCollisionShape::MakeSphere(MaxConeRadius),
		SweepQueryParams
	);

	UE_LOG(LogTemp, Warning, TEXT("Cone Hitscan: Sweep found %d hits, MaxRadius=%.1f, MaxDist=%.0f, Angle=%.1f"),
		SweepHits.Num(), MaxConeRadius, MaxDistance, DivergenceAngle);

	// ===== ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¨ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ 3: ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¤ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° =====
	// Best target tracking (single-target: only damage the most central enemy)
	AActor* BestTarget = nullptr;
	FHitResult BestHit;
	FVector BestHitLocation = FVector::ZeroVector;
	float BestHitDistance = 0.0f;
	float BestAngle = MAX_FLT;
	bool bBestIsHeadshot = false;
	FVector BestToHitDir = FVector::ZeroVector;

	for (const FHitResult& Hit : SweepHits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitTargets.Contains(HitActor))
		{
			continue;
		}

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ
		FVector HitLocation = Hit.ImpactPoint;
		float HitDistance = Hit.Distance;

		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â sweep ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ Distance ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ 0 ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
		if (HitDistance < 1.0f)
		{
			HitDistance = FVector::Dist(Start, HitActor->GetActorLocation());
			HitLocation = HitActor->GetActorLocation();
		}

		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âº ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â
		FVector ToHit = HitLocation - Start;
		FVector ToHitDir = ToHit.GetSafeNormal();

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» - ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
		float DotProduct = FVector::DotProduct(Direction, ToHitDir);
		float AngleToHit = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(DotProduct, -1.0f, 1.0f)));

		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		float ConeRadiusAtDistance = CalculateWaveRadius(HitDistance);

		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â
		FVector PointOnAxis = Start + Direction * HitDistance;
		float DistanceFromAxis = FVector::Dist(HitLocation, PointOnAxis);

		UE_LOG(LogTemp, Warning, TEXT("  - %s: Dist=%.0f, Angle=%.1fÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â°, DistFromAxis=%.1f, ConeRadius=%.1f"),
			*HitActor->GetName(), HitDistance, AngleToHit, DistanceFromAxis, ConeRadiusAtDistance);

#if DEBUG_CONE_HITSCAN
		// ===== DEBUG: ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ =====
		const float DebugDuration = 2.0f;
		const bool bPersistent = false;
		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âº ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		DrawDebugLine(GetWorld(), Start, HitLocation, FColor::White, bPersistent, DebugDuration, 0, 1.0f);
		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		DrawDebugSphere(GetWorld(), PointOnAxis, 8.0f, 6, FColor::Magenta, bPersistent, DebugDuration);
		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ DistanceFromAxis)
		DrawDebugLine(GetWorld(), PointOnAxis, HitLocation, FColor::Magenta, bPersistent, DebugDuration, 0, 2.0f);
		// ===== END DEBUG =====
#endif

		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸:
		// 1) ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â£ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œ
		// 2) ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		bool bInsideCone = (DotProduct >= CosHalfAngle) || (DistanceFromAxis <= ConeRadiusAtDistance);

		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬)
		if (HitDistance < 200.0f)
		{
			bInsideCone = true; // ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼
		}

		if (!bInsideCone)
		{
			UE_LOG(LogTemp, Warning, TEXT("    OUTSIDE CONE"));
#if DEBUG_CONE_HITSCAN
			// DEBUG: ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
			DrawDebugSphere(GetWorld(), HitLocation, 20.0f, 8, FColor::Red, false, 2.0f);
#endif
			continue;
		}

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½
		FHitResult BlockCheck;
		FCollisionQueryParams BlockQueryParams;
		BlockQueryParams.AddIgnoredActor(this);
		BlockQueryParams.AddIgnoredActor(GetOwner());
		BlockQueryParams.AddIgnoredActor(HitActor);

		bool bBlocked = GetWorld()->LineTraceSingleByChannel(
			BlockCheck,
			Start,
			HitLocation,
			ECC_Visibility,
			BlockQueryParams
		);

		if (bBlocked && BlockCheck.Distance < HitDistance - 50.0f)
		{
			UE_LOG(LogTemp, Warning, TEXT("    BLOCKED by wall at %.0f"), BlockCheck.Distance);
#if DEBUG_CONE_HITSCAN
			// DEBUG: ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
			DrawDebugSphere(GetWorld(), HitLocation, 20.0f, 8, FColor::Orange, false, 2.0f);
#endif
			continue;
		}

		// === ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¦ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¬ ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ===
		HitTargets.Add(HitActor);

#if DEBUG_CONE_HITSCAN
		// DEBUG: ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		DrawDebugSphere(GetWorld(), HitLocation, 25.0f, 12, FColor::Green, false, 2.0f);
#endif

		// Track best (most central) target
		if (AngleToHit < BestAngle)
		{
			BestAngle = AngleToHit;
			BestTarget = HitActor;
			BestHit = Hit;
			BestHitLocation = HitLocation;
			BestHitDistance = HitDistance;
			bBestIsHeadshot = (Hit.BoneName == FName("head") || Hit.BoneName == FName("Head"));
			BestToHitDir = ToHitDir;
		}
	}

	// ===== PASS 2: Apply damage to the best (most central) target only =====
	if (BestTarget)
	{
		// Calculate wave radius at target distance
		float WaveRadiusAtTarget = CalculateWaveRadius(BestHitDistance);
		float TotalDistance = BestHitDistance;

		if (ReflectionCount > 0)
		{
			float OriginalEnergy = 1.0f;
			for (int32 i = 0; i < ReflectionCount; i++)
			{
				OriginalEnergy *= (1.0f - ReflectionEnergyLoss);
			}
			float PreviousDistance = MaxHitscanRange * (1.0f - RemainingEnergy / OriginalEnergy);
			TotalDistance = PreviousDistance + BestHitDistance;
		}

		float AreaMultiplier = CalculateDamageMultiplier(TotalDistance, WaveRadiusAtTarget);

		// Headshot check
		float HeadshotMult = bBestIsHeadshot ? HeadshotMultiplier : 1.0f;

		// Heat System multiplier
		float HeatMult = bUseHeatSystem ? CalculateHeatDamageMultiplier() : 1.0f;

		// Z-Factor multiplier
		float ZFactorMult = 1.0f;
		if (bUseZFactor && PawnOwner)
		{
			float ShooterZ = PawnOwner->GetActorLocation().Z;
			float TargetZ = BestTarget->GetActorLocation().Z;
			ZFactorMult = CalculateZFactorMultiplier(ShooterZ, TargetZ);
		}

		// Tag-based damage multiplier
		float TagMult = GetTagDamageMultiplier(BestTarget);

		float FinalDamage = HitscanDamage * RemainingEnergy * AreaMultiplier * HeadshotMult * HeatMult * ZFactorMult * TagMult;

		UE_LOG(LogTemp, Warning, TEXT("    BEST TARGET HIT: %s | Damage: %.1f x Energy:%.2f x Area:%.2f x HS:%.1f x Heat:%.2f x Z:%.2f x Tag:%.2f = %.1f"),
			*BestTarget->GetName(), HitscanDamage, RemainingEnergy, AreaMultiplier, HeadshotMult, HeatMult, ZFactorMult, TagMult, FinalDamage);

		// Apply damage
		FDamageEvent DamageEvent;
		if (HitscanDamageType)
		{
			DamageEvent.DamageTypeClass = HitscanDamageType;
		}

		float ActualDamage = BestTarget->TakeDamage(FinalDamage, DamageEvent, PawnOwner ? PawnOwner->GetController() : nullptr, this);

		// Notify weapon owner about hit (for hitmarkers)
		if (WeaponOwner && ActualDamage > 0.0f)
		{
			bool bKilled = IsActorDeadAfterDamage(BestTarget);
			WeaponOwner->OnWeaponHit(BestHitLocation, BestToHitDir, ActualDamage, bBestIsHeadshot, bKilled);
		}

		// Apply physics impulse
		if (UPrimitiveComponent* HitComp = BestHit.GetComponent())
		{
			if (HitComp->IsSimulatingPhysics())
			{
				FVector ImpulseDirection = BestToHitDir;
				float ImpulseForce = HitscanPhysicsForce * RemainingEnergy * AreaMultiplier;
				HitComp->AddImpulseAtLocation(ImpulseDirection * ImpulseForce, BestHitLocation);
			}
		}

		// Apply ionization (add positive charge to target)
		ApplyHitscanIonization(BestTarget);
	}

	// ===== ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¨ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ 4: ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ =====
	UE_LOG(LogTemp, Warning, TEXT("Cone Hitscan RESULT: %d targets hit"), HitTargets.Num());

	SpawnBeamEffect(Start, BeamEnd, RemainingEnergy);

	if (bUseWaveVisualization)
	{
		SpawnWaveFronts(Start, BeamEnd);
	}

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â­ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ
	if (bHitWall)
	{
		SpawnImpactEffect(WallHitResult.ImpactPoint, WallHitResult.ImpactNormal);

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ
		if (MaxReflections > 0 && IsMetal(WallHitResult) && ReflectionCount < MaxReflections)
		{
			FVector ReflectedDir = CalculateReflection(Direction, WallHitResult.ImpactNormal);
			float NewEnergy = RemainingEnergy * (1.0f - ReflectionEnergyLoss);

			UE_LOG(LogTemp, Warning, TEXT("Cone Hitscan: Reflecting off %s (NewEnergy: %.2f)"),
				*WallHitResult.GetActor()->GetName(), NewEnergy);

			SpawnReflectionEffect(WallHitResult.ImpactPoint, Direction, ReflectedDir);

			if (ReflectionSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, ReflectionSound, WallHitResult.ImpactPoint, NewEnergy);
			}

			FVector ReflectionStart = WallHitResult.ImpactPoint + ReflectedDir * 1.0f;
			PerformHitscan(ReflectionStart, ReflectedDir, NewEnergy, ReflectionCount + 1);
		}
	}

	// === DEBUG: ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ===
	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸:
	/*
	DrawDebugCone(GetWorld(), Start, Direction, MaxDistance, ConeHalfAngleRad, ConeHalfAngleRad,
		12, FColor::Yellow, false, 2.0f, 0, 1.0f);
	DrawDebugSphere(GetWorld(), BeamEnd, ConeRadiusAtEnd, 16, FColor::Cyan, false, 2.0f);
	*/
}

bool AShooterWeapon::IsMetal(const FHitResult& Hit) const
{
	if (MetalMaterials.Num() == 0)
	{
		return false;
	}

	UPhysicalMaterial* PhysMat = Hit.PhysMaterial.Get();
	if (!PhysMat)
	{
		return false;
	}

	return MetalMaterials.Contains(PhysMat);
}

FVector AShooterWeapon::CalculateReflection(const FVector& Direction, const FVector& Normal) const
{
	// R = D - 2(DÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â·N)N
	return Direction - 2.0f * FVector::DotProduct(Direction, Normal) * Normal;
}

void AShooterWeapon::ApplyHitscanDamage(const FHitResult& Hit, float EnergyMultiplier, float Distance, float WaveRadius)
{
	AActor* HitActor = Hit.GetActor();
	if (!HitActor)
	{
		return;
	}

	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢, ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢
	if (!bHitscanDamageOwner && HitActor == GetOwner())
	{
		return;
	}

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
	float AreaMultiplier = CalculateDamageMultiplier(Distance, WaveRadius);

	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° headshot
	bool bIsHeadshot = (Hit.BoneName == FName("head") || Hit.BoneName == FName("Head"));
	float HeadshotMult = bIsHeadshot ? HeadshotMultiplier : 1.0f;

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¤ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½
	float FinalDamage = HitscanDamage * EnergyMultiplier * AreaMultiplier * HeadshotMult;

	UE_LOG(LogTemp, Warning, TEXT("Hitscan Damage: Base=%.1f x Energy=%.2f x Area=%.2f x HS=%.1f = %.1f to %s (WaveR=%.1f, TargetR=%.1f)"),
		HitscanDamage, EnergyMultiplier, AreaMultiplier, HeadshotMult, FinalDamage,
		*HitActor->GetName(), WaveRadius, TargetEffectiveRadius);


	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½
	FDamageEvent DamageEvent;
	if (HitscanDamageType)
	{
		DamageEvent.DamageTypeClass = HitscanDamageType;
	}

	float ActualDamage = HitActor->TakeDamage(FinalDamage, DamageEvent, PawnOwner ? PawnOwner->GetController() : nullptr, this);

	bool bKilled = IsActorDeadAfterDamage(HitActor);


	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â£ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°)
	if (WeaponOwner && ActualDamage > 0.0f)
	{
		FVector HitDirection = (Hit.ImpactPoint - GetActorLocation()).GetSafeNormal();
		WeaponOwner->OnWeaponHit(Hit.ImpactPoint, HitDirection, ActualDamage, bIsHeadshot, bKilled);
	}

	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â
	if (UPrimitiveComponent* HitComp = Hit.GetComponent())
	{
		if (HitComp->IsSimulatingPhysics())
		{
			FVector ImpulseDirection = (Hit.ImpactPoint - GetActorLocation()).GetSafeNormal();
			float ImpulseForce = HitscanPhysicsForce * EnergyMultiplier * AreaMultiplier;
			HitComp->AddImpulseAtLocation(ImpulseDirection * ImpulseForce, Hit.ImpactPoint);
		}
	}

	// Apply ionization (add positive charge to target)
	ApplyHitscanIonization(HitActor);
}

float AShooterWeapon::GetTagDamageMultiplier(AActor* Target) const
{
	if (!Target || TagDamageMultipliers.Num() == 0)
	{
		return 1.0f;
	}

	float Multiplier = 1.0f;

	for (const auto& Pair : TagDamageMultipliers)
	{
		if (Target->ActorHasTag(Pair.Key))
		{
			Multiplier *= Pair.Value;
		}
	}

	return Multiplier;
}

void AShooterWeapon::ApplyHitscanIonization(AActor* Target)
{
	if (!bUseHitscanIonization || !Target)
	{
		return;
	}

	// Try UEMFVelocityModifier first (for characters/NPCs)
	if (UEMFVelocityModifier* TargetModifier = Target->FindComponentByClass<UEMFVelocityModifier>())
	{
		// Use GetCharge() to read actual FieldComponent charge (not BaseCharge which may be stale
		// after melee's SetCharge() calls that bypass BaseCharge tracking)
		const float CurrentCharge = TargetModifier->GetCharge();

		// Already at max positive charge
		if (CurrentCharge >= MaxIonizationCharge)
		{
			return;
		}

		const float NewCharge = FMath::Min(CurrentCharge + IonizationChargePerHit, MaxIonizationCharge);
		TargetModifier->SetCharge(NewCharge);
		return;
	}

	// Fallback: try raw UEMF_FieldComponent (for physics props without velocity modifier)
	if (UEMF_FieldComponent* TargetField = Target->FindComponentByClass<UEMF_FieldComponent>())
	{
		FEMSourceDescription Desc = TargetField->GetSourceDescription();
		const float CurrentCharge = Desc.PointChargeParams.Charge;

		if (CurrentCharge >= MaxIonizationCharge)
		{
			return;
		}

		Desc.PointChargeParams.Charge = FMath::Min(CurrentCharge + IonizationChargePerHit, MaxIonizationCharge);
		TargetField->SetSourceDescription(Desc);
	}
}

float AShooterWeapon::CalculateWaveRadius(float Distance) const
{
	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â£ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ WaveDivergence
	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ WaveDivergence = 0, ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» = 0 (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â)
	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ WaveDivergence = 1, ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» = MaxDivergenceAngle
	float DivergenceAngle = WaveDivergence * MaxDivergenceAngle;
	float TangentAngle = FMath::Tan(FMath::DegreesToRadians(DivergenceAngle));

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â = ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â + ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼
	float Radius = InitialWaveRadius + Distance * TangentAngle;

	return Radius;
}

float AShooterWeapon::CalculateDamageMultiplier(float Distance, float WaveRadius) const
{
	// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ <= ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸, ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢
	if (WaveRadius <= TargetEffectiveRadius)
	{
		return 1.0f;
	}

	// ÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ = (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ / ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹)
	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ~ RÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â², ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢: ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ = (TargetRadius / WaveRadius)ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â²
	float AreaRatio = (TargetEffectiveRadius * TargetEffectiveRadius) / (WaveRadius * WaveRadius);

	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼
	return FMath::Max(AreaRatio, MinDamageMultiplier);
}

// ==================== VFX ====================

float AShooterWeapon::GetOwnerCharge() const
{
	if (!PawnOwner)
	{
		return 0.0f;
	}

	UEMF_FieldComponent* FieldComp = PawnOwner->FindComponentByClass<UEMF_FieldComponent>();
	if (!FieldComp)
	{
		return 0.0f;
	}

	return FieldComp->GetSourceDescription().PointChargeParams.Charge;
}

void AShooterWeapon::SpawnMuzzleFlashEffect()
{
	// Determine which VFX to use
	UNiagaraSystem* VFXToSpawn = MuzzleFlashFX;

	// Check if charge-based muzzle flash is enabled
	if (bUseChargeMuzzleFlash)
	{
		float OwnerCharge = GetOwnerCharge();

		if (OwnerCharge > 0.0f && PositiveMuzzleFlashFX)
		{
			VFXToSpawn = PositiveMuzzleFlashFX;
		}
		else if (OwnerCharge < 0.0f && NegativeMuzzleFlashFX)
		{
			VFXToSpawn = NegativeMuzzleFlashFX;
		}
		// If charge is neutral or appropriate VFX is not set, fall back to default MuzzleFlashFX
	}

	if (!VFXToSpawn)
	{
		return;
	}

	// Spawn attached to muzzle socket so VFX follows weapon movement
	UNiagaraComponent* MuzzleComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
		VFXToSpawn,
		FirstPersonMesh,
		MuzzleSocketName,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		FVector(MuzzleFlashScale),
		EAttachLocation::SnapToTarget,
		true,
		ENCPoolMethod::None
	);

	if (MuzzleComp)
	{
		// Set muzzle flash parameters
		MuzzleComp->SetColorParameter(FName("FlashColor"), MuzzleFlashColor);
		MuzzleComp->SetFloatParameter(FName("Intensity"), MuzzleFlashIntensity);
		MuzzleComp->SetFloatParameter(FName("Duration"), MuzzleFlashDuration);

		// Pass wave-specific parameters if using wave visualization
		if (bUseWaveVisualization)
		{
			MuzzleComp->SetFloatParameter(FName("Wavelength"), Wavelength);
			MuzzleComp->SetFloatParameter(FName("Amplitude"), Amplitude);
			MuzzleComp->SetColorParameter(FName("EFieldColor"), EFieldColor);
			MuzzleComp->SetColorParameter(FName("BFieldColor"), BFieldColor);
		}

		// Pass beam color for consistency
		MuzzleComp->SetColorParameter(FName("BeamColor"), BeamColor);
	}
}

void AShooterWeapon::SpawnBeamEffect(const FVector& Start, const FVector& End, float EnergyMultiplier)
{
	if (!BeamFX)
	{
		return;
	}

	UNiagaraComponent* BeamComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		BeamFX,
		Start,
		(End - Start).Rotation(),
		FVector::OneVector,
		true,
		true,
		ENCPoolMethod::None
	);

	if (BeamComp)
	{
		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹
		BeamComp->SetVectorParameter(FName("BeamStart"), Start);
		BeamComp->SetVectorParameter(FName("BeamEnd"), End);
		BeamComp->SetFloatParameter(FName("Energy"), EnergyMultiplier);
		BeamComp->SetColorParameter(FName("BeamColor"), BeamColor);

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½
		FVector UpVector = FVector::UpVector;
		FVector RightVector = FVector::RightVector;

		if (PawnOwner)
		{
			if (AController* Controller = PawnOwner->GetController())
			{
				FRotator CameraRotation;
				FVector CameraLocation;
				Controller->GetPlayerViewPoint(CameraLocation, CameraRotation);

				UpVector = CameraRotation.Quaternion().GetUpVector();
				RightVector = CameraRotation.Quaternion().GetRightVector();
			}
		}

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		BeamComp->SetVectorParameter(FName("UpVector"), UpVector);
		BeamComp->SetVectorParameter(FName("RightVector"), RightVector);

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
		float BeamDistance = FVector::Distance(Start, End);
		float StartRadius = CalculateWaveRadius(0.0f);
		float EndRadius = CalculateWaveRadius(BeamDistance);

		BeamComp->SetFloatParameter(FName("StartRadius"), StartRadius);
		BeamComp->SetFloatParameter(FName("EndRadius"), EndRadius);
		BeamComp->SetFloatParameter(FName("MaxDivergenceAngle"), MaxDivergenceAngle);
		BeamComp->SetFloatParameter(FName("TargetRadius"), TargetEffectiveRadius);

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â)
		BeamComp->SetFloatParameter(FName("WaveDivergence"), WaveDivergence);
		BeamComp->SetFloatParameter(FName("MaxRange"), MaxHitscanRange);
		BeamComp->SetFloatParameter(FName("MinEnergy"), MinDamageMultiplier);

		// Wave-ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹
		if (bUseWaveVisualization)
		{
			BeamComp->SetFloatParameter(FName("Wavelength"), Wavelength);
			BeamComp->SetFloatParameter(FName("Amplitude"), Amplitude);
			BeamComp->SetFloatParameter(FName("FadeTime"), BeamFadeTime);
			BeamComp->SetFloatParameter(FName("WavePacketLength"), WavePacketLength);
			BeamComp->SetFloatParameter(FName("WavePacketDelay"), WavePacketDelay);
			BeamComp->SetFloatParameter(FName("WavePacketSpeed"), WavePacketSpeed);
			BeamComp->SetColorParameter(FName("EFieldColor"), EFieldColor);
			BeamComp->SetColorParameter(FName("BFieldColor"), BFieldColor);
		}
	}
}

void AShooterWeapon::SpawnWaveFronts(const FVector& Start, const FVector& End)
{
	if (!WaveFrontFX)
	{
		return;
	}

	FVector Direction = (End - Start).GetSafeNormal();
	float Distance = FVector::Distance(Start, End);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	float StartRadius = CalculateWaveRadius(0.0f);  // ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ (InitialWaveRadius)
	float EndRadius = CalculateWaveRadius(Distance); // ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ
	float DivergenceAngle = WaveDivergence * MaxDivergenceAngle;

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ Niagara ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ
	UNiagaraComponent* ConeComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		WaveFrontFX,
		Start,
		Direction.Rotation(),
		FVector::OneVector,
		true,
		true,
		ENCPoolMethod::None
	);

	if (ConeComp)
	{
		// === ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ===
		ConeComp->SetVectorParameter(FName("BeamStart"), Start);
		ConeComp->SetVectorParameter(FName("BeamEnd"), End);
		ConeComp->SetVectorParameter(FName("BeamDirection"), Direction);
		ConeComp->SetFloatParameter(FName("MaxDistance"), Distance);
		ConeComp->SetFloatParameter(FName("InitialRadius"), StartRadius);
		ConeComp->SetFloatParameter(FName("EndRadius"), EndRadius);
		ConeComp->SetFloatParameter(FName("DivergenceAngle"), DivergenceAngle);

		// === ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ===
		ConeComp->SetFloatParameter(FName("TravelSpeed"), WavePacketSpeed);
		ConeComp->SetFloatParameter(FName("Lifetime"), BeamFadeTime);
		ConeComp->SetFloatParameter(FName("ExpansionSpeed"), WaveFrontExpansionSpeed);

		// === ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ===
		ConeComp->SetColorParameter(FName("WaveColor"), EFieldColor);
		ConeComp->SetFloatParameter(FName("Wavelength"), Wavelength);
		ConeComp->SetFloatParameter(FName("Energy"), 1.0f);

		// === ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°) ===
		FVector RightVector = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
		if (RightVector.IsNearlyZero())
		{
			RightVector = FVector::CrossProduct(Direction, FVector::RightVector).GetSafeNormal();
		}
		FVector UpVector = FVector::CrossProduct(RightVector, Direction).GetSafeNormal();

		ConeComp->SetVectorParameter(FName("UpVector"), UpVector);
		ConeComp->SetVectorParameter(FName("RightVector"), RightVector);

		// === ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ===
		ConeComp->SetFloatParameter(FName("WaveDivergence"), WaveDivergence);
		ConeComp->SetFloatParameter(FName("MinDamageMultiplier"), MinDamageMultiplier);
	}
}

void AShooterWeapon::SpawnImpactEffect(const FVector& Location, const FVector& Normal)
{
	if (!ImpactFX)
	{
		return;
	}

	UNiagaraComponent* ImpactComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		ImpactFX,
		Location,
		Normal.Rotation(),
		FVector::OneVector,
		true,
		true,
		ENCPoolMethod::None
	);

	if (ImpactComp)
	{
		ImpactComp->SetColorParameter(FName("ImpactColor"), BeamColor);

		if (bUseWaveVisualization)
		{
			ImpactComp->SetFloatParameter(FName("Wavelength"), Wavelength);
		}
	}
}

void AShooterWeapon::SpawnReflectionEffect(const FVector& Location, const FVector& IncomingDirection, const FVector& ReflectedDirection)
{
	if (!ReflectionFX)
	{
		return;
	}

	UNiagaraComponent* ReflectionComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		ReflectionFX,
		Location,
		FRotator::ZeroRotator,
		FVector::OneVector,
		true,
		true,
		ENCPoolMethod::None
	);

	if (ReflectionComp)
	{
		ReflectionComp->SetVectorParameter(FName("IncomingDirection"), IncomingDirection);
		ReflectionComp->SetVectorParameter(FName("ReflectedDirection"), ReflectedDirection);
		ReflectionComp->SetColorParameter(FName("FlashColor"), BeamColor);
	}
}

// ==================== SFX ====================

void AShooterWeapon::PlayFireSound()
{
	if (!FireSound)
	{
		return;
	}

	// Get muzzle location for 3D sound
	// Use ThirdPersonMesh for NPCs (visible to player), FirstPersonMesh for local player
	FVector MuzzleLocation;

	bool bIsLocalPlayer = false;
	if (PawnOwner)
	{
		APlayerController* PC = Cast<APlayerController>(PawnOwner->GetController());
		bIsLocalPlayer = PC && PC->IsLocalController();
	}

	if (bIsLocalPlayer && FirstPersonMesh)
	{
		MuzzleLocation = FirstPersonMesh->GetSocketLocation(MuzzleSocketName);
	}
	else if (ThirdPersonMesh)
	{
		MuzzleLocation = ThirdPersonMesh->GetSocketLocation(MuzzleSocketName);
	}
	else
	{
		// Fallback to owner location
		MuzzleLocation = GetOwner()->GetActorLocation();
	}

	// Calculate random pitch within specified range
	const float RandomPitch = FMath::RandRange(FireSoundPitchMin, FireSoundPitchMax);

	// Play sound at muzzle location with attenuation for proper 3D spatialization
	UGameplayStatics::SpawnSoundAtLocation(
		this,
		FireSound,
		MuzzleLocation,
		FRotator::ZeroRotator,
		FireSoundVolume,
		RandomPitch,
		0.0f,  // StartTime
		FireSoundAttenuation
	);
}

float AShooterWeapon::GetOptimalDamageRange() const
{
	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ WaveRadius == TargetEffectiveRadius
	// WaveRadius = InitialWaveRadius + Distance * tan(DivergenceAngle)
	// TargetRadius = InitialRadius + OptimalDistance * tan(Angle)
	// OptimalDistance = (TargetRadius - InitialRadius) / tan(Angle)

	float DivergenceAngle = WaveDivergence * MaxDivergenceAngle;
	float TangentAngle = FMath::Tan(FMath::DegreesToRadians(DivergenceAngle));

	if (TangentAngle <= KINDA_SMALL_NUMBER)
	{
		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		return MaxHitscanRange;
	}

	float OptimalDistance = (TargetEffectiveRadius - InitialWaveRadius) / TangentAngle;
	return FMath::Max(0.0f, OptimalDistance);
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetFirstPersonAnimInstanceClass() const
{
	return FirstPersonAnimInstanceClass;
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetThirdPersonAnimInstanceClass() const
{
	return ThirdPersonAnimInstanceClass;
}

void AShooterWeapon::PlayADSInSound()
{
	if (!ADSInSound)
	{
		return;
	}

	// Get weapon location for 3D sound
	const FVector WeaponLocation = FirstPersonMesh->GetComponentLocation();

	// Calculate random pitch within specified range
	const float RandomPitch = FMath::RandRange(ADSSoundPitchMin, ADSSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		ADSInSound,
		WeaponLocation,
		FRotator::ZeroRotator,
		ADSSoundVolume,
		RandomPitch
	);
}

void AShooterWeapon::PlayADSOutSound()
{
	if (!ADSOutSound)
	{
		return;
	}

	// Get weapon location for 3D sound
	const FVector WeaponLocation = FirstPersonMesh->GetComponentLocation();

	// Calculate random pitch within specified range
	const float RandomPitch = FMath::RandRange(ADSSoundPitchMin, ADSSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		ADSOutSound,
		WeaponLocation,
		FRotator::ZeroRotator,
		ADSSoundVolume,
		RandomPitch
	);
}

// ==================== Heat System ====================

void AShooterWeapon::UpdateHeat(float DeltaTime)
{
	if (CurrentHeat <= 0.0f)
	{
		// Deactivate VFX when cold
		if (HeatVFXComponent && HeatVFXComponent->IsActive())
		{
			HeatVFXComponent->Deactivate();
		}
		return;
	}

	// Calculate decay rate based on owner speed
	float SpeedRatio = FMath::Clamp(GetOwnerSpeed() / MaxSpeedForHeatBonus, 0.0f, 1.0f);
	float SpeedBonus = 1.0f + (SpeedHeatDecayBonus * SpeedRatio);
	float DecayRate = BaseHeatDecayRate * SpeedBonus;

	// Apply decay
	CurrentHeat = FMath::Max(0.0f, CurrentHeat - DecayRate * DeltaTime);

	// Update Heat VFX
	UpdateHeatVFX();
}

void AShooterWeapon::UpdateHeatVFX()
{
	// Skip if no VFX system configured
	if (!HeatVFX)
	{
		return;
	}

	// Check if heat is above threshold
	if (CurrentHeat >= HeatVFXThreshold)
	{
		// Spawn VFX if not active
		if (!HeatVFXComponent)
		{
			USkeletalMeshComponent* AttachMesh = FirstPersonMesh;

			HeatVFXComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
				HeatVFX,
				AttachMesh,
				HeatVFXSocket,
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				EAttachLocation::SnapToTarget,
				false // Don't auto-destroy, we manage lifecycle
			);
		}
		else if (!HeatVFXComponent->IsActive())
		{
			HeatVFXComponent->Activate();
		}

		// Update heat parameter
		if (HeatVFXComponent)
		{
			HeatVFXComponent->SetFloatParameter(HeatParameterName, CurrentHeat);
		}
	}
	else
	{
		// Below threshold - deactivate VFX
		if (HeatVFXComponent && HeatVFXComponent->IsActive())
		{
			HeatVFXComponent->Deactivate();
		}
	}
}

void AShooterWeapon::AddHeat(float Amount)
{
	CurrentHeat = FMath::Clamp(CurrentHeat + Amount, 0.0f, 1.0f);
}

float AShooterWeapon::GetOwnerSpeed() const
{
	if (CachedMovementComponent)
	{
		return CachedMovementComponent->Velocity.Size();
	}

	if (PawnOwner)
	{
		return PawnOwner->GetVelocity().Size();
	}

	return 0.0f;
}

float AShooterWeapon::CalculateHeatDamageMultiplier() const
{
	// Lerp from 1.0 (no heat) to MinHeatDamageMultiplier (max heat)
	return FMath::Lerp(1.0f, MinHeatDamageMultiplier, CurrentHeat);
}

float AShooterWeapon::CalculateHeatFireRateMultiplier() const
{
	if (!bUseHeatSystem)
	{
		return 1.0f;
	}
	// Lerp from 1.0 (no heat, normal fire rate) to MaxHeatFireRateMultiplier (max heat, slower fire rate)
	return FMath::Lerp(1.0f, MaxHeatFireRateMultiplier, CurrentHeat);
}

float AShooterWeapon::GetCurrentRefireRate() const
{
	// Base refire rate multiplied by heat penalty
	return RefireRate * CalculateHeatFireRateMultiplier();
}

// ==================== Z-Factor ====================

float AShooterWeapon::CalculateZFactorMultiplier(float ShooterZ, float TargetZ) const
{
	// Calculate height difference (positive = shooter is above)
	float HeightDiff = ShooterZ - TargetZ;

	// No bonus if shooter is below or at same level
	if (HeightDiff <= ZFactorMinHeightDiff)
	{
		return 1.0f;
	}

	// Calculate normalized height difference
	float EffectiveHeightDiff = HeightDiff - ZFactorMinHeightDiff;
	float MaxEffectiveHeightDiff = ZFactorMaxHeightDiff - ZFactorMinHeightDiff;
	float HeightRatio = FMath::Clamp(EffectiveHeightDiff / MaxEffectiveHeightDiff, 0.0f, 1.0f);

	// Lerp from 1.0 to ZFactorMaxMultiplier based on height
	return FMath::Lerp(1.0f, ZFactorMaxMultiplier, HeightRatio);
}

// ==================== ADS Camera ====================

void AShooterWeapon::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	// This is called by PlayerCameraManager when this weapon is the ViewTarget (during ADS).
	// We provide the sight socket's WORLD POSITION but use ControlRotation for camera direction.
	// This way the camera sits at the weapon's sight, but does NOT inherit visual recoil kick
	// from the hands mesh — only the spring-smoothed camera recoil via AddPitchInput affects it.

	if (!ADSCameraComponent || !PawnOwner)
	{
		Super::CalcCamera(DeltaTime, OutResult);
		return;
	}

	// Use the ADS camera component's world position (attached to Sight socket on FP mesh).
	// This position includes the recoil visual kick (since FP mesh is moved by it).
	// We subtract the recoil offset to get the "clean" sight position.
	FVector SightWorldLocation = ADSCameraComponent->GetComponentLocation();

	// Subtract recoil visual kick from the sight position.
	// The weapon owner's RecoilComponent applies offsets to the FP Mesh,
	// which moves the ADS camera too. We want the camera without that kick.
	if (ACharacter* CharOwner = Cast<ACharacter>(PawnOwner))
	{
		if (UWeaponRecoilComponent* Recoil = CharOwner->FindComponentByClass<UWeaponRecoilComponent>())
		{
			// GetWeaponOffset returns offset in world-logical space (X=forward, Y=right, Z=up)
			FVector RecoilWorldOffset = Recoil->GetWeaponOffset();
			SightWorldLocation -= RecoilWorldOffset;
		}
	}

	// Use ControlRotation — this includes spring camera recoil (via AddPitchInput) but NOT
	// the visual weapon kick (which only affects FP Mesh relative transform).
	FRotator CameraRotation = PawnOwner->GetControlRotation();

	OutResult.Location = SightWorldLocation;
	OutResult.Rotation = CameraRotation;

	// FOV — use weapon's custom ADS FOV if set, otherwise use ADSFOVMultiplier * base FOV.
	// PlayerCameraManager blends between character camera FOV and this FOV automatically.
	if (CustomADSFOV > 0.0f)
	{
		OutResult.FOV = CustomADSFOV;
	}
	else
	{
		// Get the character's base FOV and apply multiplier
		float BaseFOV = 90.0f; // default fallback
		if (ACharacter* CharOwner = Cast<ACharacter>(PawnOwner))
		{
			if (UCameraComponent* CharCamera = CharOwner->FindComponentByClass<UCameraComponent>())
			{
				// Use the camera's default FOV (not current, since it might be mid-blend)
				BaseFOV = CharCamera->FieldOfView;
			}
		}
		OutResult.FOV = BaseFOV * ADSFOVMultiplier;
	}
}

// ==================== Charge-Based Firing ====================

bool AShooterWeapon::TryConsumeCharge(float& OutChargeMultiplier)
{
	OutChargeMultiplier = 1.0f;

	if (!bUseChargeFiring)
	{
		return true; // Not using charge system
	}

	// Find owner's EMFVelocityModifier
	AActor* WeaponOwnerActor = GetOwner();
	if (!WeaponOwnerActor)
	{
		return false;
	}

	UEMFVelocityModifier* EMFMod = WeaponOwnerActor->FindComponentByClass<UEMFVelocityModifier>();
	if (!EMFMod)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShooterWeapon: Owner has no UEMFVelocityModifier for charge-based firing"));
		return false;
	}

	// Get current total charge (base + bonus)
	float ChargeModule = FMath::Abs(EMFMod->GetCharge());

	// Check if we can afford full shot
	if (ChargeModule >= ChargePerShot + MinimumBaseCharge)
	{
		// Full power shot - deduct charge (bonus first, then base)
		OutChargeMultiplier = 1.0f;
		EMFMod->DeductCharge(ChargePerShot);

		UE_LOG(LogTemp, Log, TEXT("ShooterWeapon: Full power shot, charge module: %.2f -> %.2f"),
			ChargeModule, FMath::Abs(EMFMod->GetCharge()));
		return true;
	}
	else
	{
		// Not enough for full shot
		float AvailableCharge = FMath::Max(0.0f, ChargeModule - MinimumBaseCharge);

		if (AvailableCharge <= 0.0f || bBlockFiringBelowMinimum)
		{
			// Can't fire at all
			UE_LOG(LogTemp, Warning, TEXT("ShooterWeapon: Not enough charge to fire (have %.2f, need %.2f + %.2f minimum)"),
				ChargeModule, ChargePerShot, MinimumBaseCharge);
			return false;
		}

		// Fire weakened shot
		OutChargeMultiplier = AvailableCharge / ChargePerShot;

		// Deduct all available charge (bonus first, then base, down to minimum)
		EMFMod->DeductCharge(AvailableCharge);

		UE_LOG(LogTemp, Log, TEXT("ShooterWeapon: Weakened shot (%.1f%% power), charge module: %.2f -> %.2f"),
			OutChargeMultiplier * 100.0f, ChargeModule, FMath::Abs(EMFMod->GetCharge()));
		return true;
	}
}