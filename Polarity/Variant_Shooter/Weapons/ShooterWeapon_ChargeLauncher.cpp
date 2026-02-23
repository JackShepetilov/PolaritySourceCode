// ShooterWeapon_ChargeLauncher.cpp

#include "ShooterWeapon_ChargeLauncher.h"
#include "EMFStaticCharge.h"
#include "EMFVelocityModifier.h"
#include "Variant_Shooter/WeaponRecoilComponent.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"

AShooterWeapon_ChargeLauncher::AShooterWeapon_ChargeLauncher()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AShooterWeapon_ChargeLauncher::BeginPlay()
{
	Super::BeginPlay();

	// Cache references to owner's components (same pattern as ShooterWeapon::TryConsumeCharge)
	if (AActor* OwnerActor = GetOwner())
	{
		CachedEMFMod = OwnerActor->FindComponentByClass<UEMFVelocityModifier>();
		CachedRecoilComp = OwnerActor->FindComponentByClass<UWeaponRecoilComponent>();
	}
}

void AShooterWeapon_ChargeLauncher::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateCharging(DeltaTime);
}

// ==================== Secondary Action ====================

bool AShooterWeapon_ChargeLauncher::OnSecondaryAction()
{
	// Already charging — block ADS
	if (bIsCharging)
	{
		return true;
	}

	// Need charge to start
	if (!CachedEMFMod || FMath::IsNearlyZero(FMath::Abs(CachedEMFMod->GetCharge())))
	{
		return false; // No charge — allow normal ADS (or nothing)
	}

	StartCharging();
	return true; // Block ADS
}

void AShooterWeapon_ChargeLauncher::OnSecondaryActionReleased()
{
	if (bIsCharging)
	{
		StopCharging(false);
	}
}

// ==================== Charging Logic ====================

void AShooterWeapon_ChargeLauncher::StartCharging()
{
	bIsCharging = true;
	ChargeStartTime = GetWorld()->GetTimeSeconds();
	AccumulatedCharge = 0.0f;

	// Enhanced sway
	if (CachedRecoilComp)
	{
		CachedRecoilComp->SetSwayOverrideMultiplier(ChargingSwayMultiplier);
	}

	// Start looping charge sound
	if (ChargingLoopSound && GetFirstPersonMesh())
	{
		ChargingAudioComponent = UGameplayStatics::SpawnSoundAttached(
			ChargingLoopSound, GetFirstPersonMesh(), MuzzleSocketName);
	}
}

void AShooterWeapon_ChargeLauncher::UpdateCharging(float DeltaTime)
{
	if (!bIsCharging || !CachedEMFMod)
	{
		return;
	}

	const float AvailableCharge = FMath::Abs(CachedEMFMod->GetCharge());
	const float DesiredConsumption = ChargeConsumedPerSecond * DeltaTime;
	const float ActualConsumption = FMath::Min(DesiredConsumption, AvailableCharge);

	if (ActualConsumption > KINDA_SMALL_NUMBER)
	{
		CachedEMFMod->DeductCharge(ActualConsumption);
		AccumulatedCharge += ActualConsumption;
	}

	// Auto-release if charge depleted
	if (FMath::Abs(CachedEMFMod->GetCharge()) < KINDA_SMALL_NUMBER)
	{
		StopCharging(true);
	}
}

void AShooterWeapon_ChargeLauncher::StopCharging(bool bAutoRelease)
{
	if (!bIsCharging)
	{
		return;
	}

	bIsCharging = false;

	// Restore normal sway
	if (CachedRecoilComp)
	{
		CachedRecoilComp->SetSwayOverrideMultiplier(1.0f);
	}

	// Stop looping sound
	if (ChargingAudioComponent)
	{
		ChargingAudioComponent->Stop();
		ChargingAudioComponent = nullptr;
	}

	// Check minimum hold time (skip for auto-release — player ran out of charge)
	const float HoldDuration = GetWorld()->GetTimeSeconds() - ChargeStartTime;
	if (HoldDuration < MinHoldTime && !bAutoRelease)
	{
		CancelCharge();
		return;
	}

	// Need accumulated charge to spawn
	if (AccumulatedCharge < KINDA_SMALL_NUMBER)
	{
		CancelCharge();
		return;
	}

	SpawnStaticCharge();
}

void AShooterWeapon_ChargeLauncher::CancelCharge()
{
	// Consumed charge is lost — no refund
	AccumulatedCharge = 0.0f;

	if (ChargeCancelSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), ChargeCancelSound, GetActorLocation());
	}
}

void AShooterWeapon_ChargeLauncher::SpawnStaticCharge()
{
	if (!StaticChargeClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("ChargeLauncher: StaticChargeClass is not set!"));
		AccumulatedCharge = 0.0f;
		return;
	}

	// Muzzle location (same logic as CalculateProjectileSpawnTransform)
	USkeletalMeshComponent* MuzzleMesh = (PawnOwner && PawnOwner->IsPlayerControlled())
		? GetFirstPersonMesh() : GetThirdPersonMesh();

	FVector MuzzleLocation = FVector::ZeroVector;
	if (MuzzleMesh && MuzzleMesh->DoesSocketExist(MuzzleSocketName))
	{
		MuzzleLocation = MuzzleMesh->GetSocketLocation(MuzzleSocketName);
	}
	else if (GetOwner())
	{
		MuzzleLocation = GetOwner()->GetActorLocation();
	}

	// Aim direction: muzzle → crosshair target (same as projectile fire)
	FVector AimDirection = FVector::ForwardVector;
	if (WeaponOwner)
	{
		const FVector TargetLocation = WeaponOwner->GetWeaponTargetLocation();
		AimDirection = (TargetLocation - MuzzleLocation).GetSafeNormal();
	}

	const FVector SpawnLocation = MuzzleLocation + AimDirection * SpawnDistance;

	// Spawn the static charge
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = PawnOwner;

	AEMFStaticCharge* StaticCharge = GetWorld()->SpawnActor<AEMFStaticCharge>(
		StaticChargeClass, SpawnLocation, AimDirection.Rotation(), SpawnParams);

	if (StaticCharge)
	{
		// Charge sign matches player, magnitude = accumulated charge
		float PlayerSign = 1.0f;
		if (CachedEMFMod)
		{
			PlayerSign = static_cast<float>(CachedEMFMod->GetChargeSign());
		}
		StaticCharge->SetCharge(PlayerSign * AccumulatedCharge);
	}

	// Release sound
	if (ChargeReleaseSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), ChargeReleaseSound, SpawnLocation);
	}

	AccumulatedCharge = 0.0f;
}
