// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_360Shot.h"
#include "UpgradeDefinition_360Shot.h"
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
	// Cache the typed definition
	Def360 = Cast<UUpgradeDefinition_360Shot>(UpgradeDefinition);
	if (!Def360.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("360 Shot: UpgradeDefinition is not UUpgradeDefinition_360Shot!"));
		return;
	}

	bFirstFrame = true;
	AccumulatedYaw = 0.0f;
	bIsCharged = false;

	SetComponentTickEnabled(true);
}

void UUpgrade_360Shot::OnUpgradeDeactivated()
{
	SetComponentTickEnabled(false);
	DeactivateCharged();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChargedExpirationTimer);
	}
}

void UUpgrade_360Shot::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Def360.IsValid())
	{
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

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
	if (RotationSpeed >= Def360->MinRotationSpeed)
	{
		AccumulatedYaw += AbsDelta;
		TimeSinceLastSignificantRotation = 0.0f;
	}
	else
	{
		TimeSinceLastSignificantRotation += DeltaTime;
		if (TimeSinceLastSignificantRotation > Def360->SpinTimeWindow)
		{
			AccumulatedYaw = 0.0f;
		}
	}

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
	if (!Def360.IsValid())
	{
		return;
	}

	bIsCharged = true;
	AccumulatedYaw = 0.0f;

	// Play ready sound
	if (Def360->ChargedReadySound)
	{
		AShooterCharacter* Character = GetShooterCharacter();
		if (Character)
		{
			UGameplayStatics::PlaySoundAtLocation(this, Def360->ChargedReadySound, Character->GetActorLocation());
		}
	}

	// Start expiration timer
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ChargedExpirationTimer, this, &UUpgrade_360Shot::DeactivateCharged,
			Def360->ChargedDuration, false);
	}

	UE_LOG(LogTemp, Log, TEXT("360 Shot: CHARGED! %.1fs window"), Def360->ChargedDuration);
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

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChargedExpirationTimer);
	}

	UE_LOG(LogTemp, Log, TEXT("360 Shot: Discharged"));
}

void UUpgrade_360Shot::Execute360Shot()
{
	if (!Def360.IsValid())
	{
		return;
	}

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

	// Trace from camera viewpoint (same as weapon's FireHitscan does)
	FVector ViewLocation = Character->GetPawnViewLocation();
	FVector ViewDirection = Character->GetBaseAimRotation().Vector();

	FVector TraceEnd = ViewLocation + ViewDirection * 20000.0f;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Character);
	QueryParams.AddIgnoredActor(Weapon);
	QueryParams.bReturnPhysicalMaterial = true;

	// Trace by ECC_Pawn to hit NPC pawns (same as weapon's cone sweep).
	// ECC_Visibility hits static meshes/walls first, missing NPCs behind them.
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	FHitResult HitResult;
	bool bHit = GetWorld()->LineTraceSingleByObjectType(
		HitResult, ViewLocation, TraceEnd, ObjectParams, QueryParams);

	FVector BeamEnd = bHit ? HitResult.ImpactPoint : TraceEnd;

	// Get muzzle location for VFX only (not for trace)
	FVector MuzzleLocation = ViewLocation;
	USkeletalMeshComponent* WeaponMesh = Weapon->GetFirstPersonMesh();
	if (WeaponMesh)
	{
		MuzzleLocation = WeaponMesh->GetSocketLocation(FName("Muzzle"));
	}

	// Spawn charged beam VFX (from muzzle to hit point)
	SpawnChargedBeamEffect(MuzzleLocation, BeamEnd);

	// Play charged fire sound
	if (Def360->ChargedFireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Def360->ChargedFireSound, MuzzleLocation);
	}

	// Apply bonus damage if we hit something
	if (bHit && HitResult.GetActor())
	{
		AActor* HitActor = HitResult.GetActor();

		FDamageEvent DamageEvent;
		DamageEvent.DamageTypeClass = UDamageType_Ranged::StaticClass();

		float ActualDamage = HitActor->TakeDamage(
			Def360->BonusDamage, DamageEvent,
			Controller, Weapon);

		UE_LOG(LogTemp, Log, TEXT("360 Shot: Dealt %.0f bonus damage to %s"), ActualDamage, *HitActor->GetName());
	}
}

void UUpgrade_360Shot::SpawnChargedBeamEffect(const FVector& Start, const FVector& End)
{
	if (!Def360.IsValid() || !Def360->ChargedBeamFX)
	{
		UE_LOG(LogTemp, Warning, TEXT("360 Shot: No ChargedBeamFX set, skipping beam VFX"));
		return;
	}

	UNiagaraComponent* BeamComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		Def360->ChargedBeamFX,
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
		BeamComp->SetColorParameter(FName("BeamColor"), Def360->ChargedBeamColor);
	}
}
