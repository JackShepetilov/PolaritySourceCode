// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_360Shot.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "ShooterWeapon_Laser.h"
#include "UpgradeManagerComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Engine/DamageEvents.h"
#include "Variant_Shooter/DamageTypes/DamageType_Ranged.h"

UUpgrade_360Shot::UUpgrade_360Shot()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UUpgrade_360Shot::OnUpgradeActivated()
{
	bFirstFrame = true;
	AccumulatedYaw = 0.0f;
	bIsCharged = false;

	// Enable tick
	SetComponentTickEnabled(true);
}

void UUpgrade_360Shot::OnUpgradeDeactivated()
{
	SetComponentTickEnabled(false);
	DeactivateCharged();

	// Clear timer
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChargedExpirationTimer);
	}
}

void UUpgrade_360Shot::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	// Don't track rotation while already charged
	if (bIsCharged)
	{
		return;
	}

	AController* Controller = Character->GetController();
	if (!Controller)
	{
		return;
	}

	float CurrentYaw = Controller->GetControlRotation().Yaw;

	if (bFirstFrame)
	{
		PreviousYaw = CurrentYaw;
		bFirstFrame = false;
		return;
	}

	// Calculate yaw delta (handle wraparound)
	float YawDelta = CurrentYaw - PreviousYaw;
	if (YawDelta > 180.0f)
	{
		YawDelta -= 360.0f;
	}
	else if (YawDelta < -180.0f)
	{
		YawDelta += 360.0f;
	}
	PreviousYaw = CurrentYaw;

	float AbsDelta = FMath::Abs(YawDelta);
	float RotationSpeed = (DeltaTime > KINDA_SMALL_NUMBER) ? (AbsDelta / DeltaTime) : 0.0f;

	// Only count rotation above minimum speed threshold
	if (RotationSpeed >= MinRotationSpeed)
	{
		AccumulatedYaw += AbsDelta;
		TimeSinceLastSignificantRotation = 0.0f;
	}
	else
	{
		// Decay accumulated rotation if player stops spinning
		TimeSinceLastSignificantRotation += DeltaTime;
		if (TimeSinceLastSignificantRotation > SpinTimeWindow)
		{
			AccumulatedYaw = 0.0f;
		}
	}

	// Check for 360 completion
	if (AccumulatedYaw >= 360.0f)
	{
		ActivateCharged();
	}
}

void UUpgrade_360Shot::OnWeaponFired()
{
	if (!bIsCharged)
	{
		return;
	}

	AShooterWeapon* Weapon = GetCurrentWeapon();
	if (!Weapon)
	{
		return;
	}

	// Only works with hitscan weapons that are NOT lasers
	if (!Weapon->IsHitscan() || Weapon->IsA<AShooterWeapon_Laser>())
	{
		return;
	}

	Execute360Shot();
	DeactivateCharged();
}

void UUpgrade_360Shot::ActivateCharged()
{
	bIsCharged = true;
	AccumulatedYaw = 0.0f;

	// Play ready sound
	if (ChargedReadySound)
	{
		AShooterCharacter* Character = GetShooterCharacter();
		if (Character)
		{
			UGameplayStatics::PlaySoundAtLocation(this, ChargedReadySound, Character->GetActorLocation());
		}
	}

	// Start expiration timer
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ChargedExpirationTimer, this, &UUpgrade_360Shot::DeactivateCharged,
			ChargedDuration, false);
	}

	UE_LOG(LogTemp, Log, TEXT("360 Shot: CHARGED! %.1fs window"), ChargedDuration);
}

void UUpgrade_360Shot::DeactivateCharged()
{
	if (!bIsCharged)
	{
		return;
	}

	bIsCharged = false;
	AccumulatedYaw = 0.0f;
	TimeSinceLastSignificantRotation = 0.0f;

	// Clear timer in case called manually (not from timer)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChargedExpirationTimer);
	}

	UE_LOG(LogTemp, Log, TEXT("360 Shot: Discharged"));
}

void UUpgrade_360Shot::Execute360Shot()
{
	AShooterCharacter* Character = GetShooterCharacter();
	AShooterWeapon* Weapon = GetCurrentWeapon();
	if (!Character || !Weapon)
	{
		return;
	}

	AController* Controller = Character->GetController();
	if (!Controller)
	{
		return;
	}

	// Get aim direction from controller (same as weapon does)
	FVector AimDirection = Controller->GetControlRotation().Vector();

	// Get muzzle location from weapon's first person mesh
	FVector MuzzleLocation = Character->GetActorLocation() + FVector(0, 0, Character->BaseEyeHeight);
	USkeletalMeshComponent* WeaponMesh = Weapon->GetFirstPersonMesh();
	if (WeaponMesh)
	{
		MuzzleLocation = WeaponMesh->GetSocketLocation(FName("Muzzle"));
	}

	// Perform line trace for the 360 shot
	FVector TraceEnd = MuzzleLocation + AimDirection * 20000.0f;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Character);
	QueryParams.AddIgnoredActor(Weapon);
	QueryParams.bTraceComplex = true;
	QueryParams.bReturnPhysicalMaterial = true;

	FHitResult HitResult;
	bool bHit = GetWorld()->LineTraceSingleByChannel(
		HitResult, MuzzleLocation, TraceEnd, ECC_Visibility, QueryParams);

	FVector BeamEnd = bHit ? HitResult.ImpactPoint : TraceEnd;

	// Spawn charged beam VFX
	SpawnChargedBeamEffect(MuzzleLocation, BeamEnd);

	// Play charged fire sound
	if (ChargedFireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ChargedFireSound, MuzzleLocation);
	}

	// Apply bonus damage if we hit something
	if (bHit && HitResult.GetActor())
	{
		AActor* HitActor = HitResult.GetActor();

		FDamageEvent DamageEvent;
		DamageEvent.DamageTypeClass = UDamageType_Ranged::StaticClass();

		float ActualDamage = HitActor->TakeDamage(
			BonusDamage, DamageEvent,
			Controller, Weapon);

		UE_LOG(LogTemp, Log, TEXT("360 Shot: Dealt %.0f bonus damage to %s"), ActualDamage, *HitActor->GetName());
	}
}

void UUpgrade_360Shot::SpawnChargedBeamEffect(const FVector& Start, const FVector& End)
{
	UNiagaraSystem* FXToUse = ChargedBeamFX;

	// Fallback to weapon's normal beam if no custom VFX set
	if (!FXToUse)
	{
		AShooterWeapon* Weapon = GetCurrentWeapon();
		if (Weapon)
		{
			// Access weapon's beam FX through its public getter (BeamFX is protected)
			// For now, just skip if no custom FX
			UE_LOG(LogTemp, Warning, TEXT("360 Shot: No ChargedBeamFX set, skipping beam VFX"));
			return;
		}
		return;
	}

	UNiagaraComponent* BeamComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		FXToUse,
		Start,
		(End - Start).Rotation(),
		FVector::OneVector,
		true,
		true,
		ENCPoolMethod::None
	);

	if (BeamComp)
	{
		BeamComp->SetVectorParameter(FName("BeamStart"), Start);
		BeamComp->SetVectorParameter(FName("BeamEnd"), End);
		BeamComp->SetFloatParameter(FName("Energy"), 1.0f);
		BeamComp->SetColorParameter(FName("BeamColor"), ChargedBeamColor);
	}
}
