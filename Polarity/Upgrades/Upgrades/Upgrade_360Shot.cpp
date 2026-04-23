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
#include "WeaponRecoilComponent.h"
#include "Buildings/BuildingMarkable.h"

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
	bOnCooldown = false;

	SetComponentTickEnabled(true);
}

void UUpgrade_360Shot::OnUpgradeDeactivated()
{
	SetComponentTickEnabled(false);
	DeactivateCharged();
	bOnCooldown = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChargedExpirationTimer);
		World->GetTimerManager().ClearTimer(CooldownTimer);
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

	if (bIsCharged || bOnCooldown)
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

	StartCooldown();
}

void UUpgrade_360Shot::StartCooldown()
{
	if (!Def360.IsValid() || Def360->CooldownDuration <= 0.0f)
	{
		return;
	}

	bOnCooldown = true;
	AccumulatedYaw = 0.0f;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			CooldownTimer, this, &UUpgrade_360Shot::OnCooldownExpired,
			Def360->CooldownDuration, false);
	}

	UE_LOG(LogTemp, Log, TEXT("360 Shot: Cooldown started (%.1fs)"), Def360->CooldownDuration);
}

void UUpgrade_360Shot::OnCooldownExpired()
{
	bOnCooldown = false;
	bFirstFrame = true;
	AccumulatedYaw = 0.0f;
	TimeSinceLastSignificantRotation = 0.0f;

	UE_LOG(LogTemp, Log, TEXT("360 Shot: Cooldown expired, ready to spin again"));
}

float UUpgrade_360Shot::GetCooldownRemaining() const
{
	if (!bOnCooldown)
	{
		return 0.0f;
	}

	if (const UWorld* World = GetWorld())
	{
		return World->GetTimerManager().GetTimerRemaining(CooldownTimer);
	}

	return 0.0f;
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

	// Trace from camera viewpoint (same as weapon's FireHitscan does).
	// Range must reach skyscrapers across the bridge (several km in level design).
	FVector ViewLocation = Character->GetPawnViewLocation();
	FVector ViewDirection = Character->GetBaseAimRotation().Vector();

	FVector TraceEnd = ViewLocation + ViewDirection * 500000.0f; // 5 km

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Character);
	QueryParams.AddIgnoredActor(Weapon);
	QueryParams.bReturnPhysicalMaterial = true;

	// Trace by ECC_Pawn to hit NPC pawns (same as weapon's cone sweep).
	// Also trace WorldStatic/WorldDynamic so the shot can land on buildings — needed for
	// IBuildingMarkable. ObjectType trace returns the closest of all listed types, so a wall
	// in front of an NPC still blocks the shot (no wall-hack regression).
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

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

	// Apply 360 shot's own recoil settings (independent from weapon's recoil)
	if (UWeaponRecoilComponent* RecoilComp = Character->GetRecoilComponent())
	{
		RecoilComp->FireWithOverrideSettings(Def360->ShotRecoilSettings);
	}

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

		// Mark the target if it implements IBuildingMarkable. The marker VFX is spawned by the
		// upgrade itself (so the same upgrade can later mark other markable target types).
		const bool bIsMarkable = HitActor->Implements<UBuildingMarkable>();
		UE_LOG(LogTemp, Warning, TEXT("[BUILDING_MARK] Hit %s | Markable=%d | ImpactPt=%s Normal=%s"),
			*HitActor->GetName(), bIsMarkable ? 1 : 0,
			*HitResult.ImpactPoint.ToCompactString(), *HitResult.ImpactNormal.ToCompactString());

		if (bIsMarkable)
		{
			if (Def360->MarkerVFX)
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(
					GetWorld(),
					Def360->MarkerVFX,
					HitResult.ImpactPoint,
					HitResult.ImpactNormal.Rotation(),
					FVector::OneVector,
					true,   // bAutoDestroy
					true,   // bAutoActivate
					ENCPoolMethod::None);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[BUILDING_MARK] MarkerVFX is null on UpgradeDefinition_360Shot — no visual marker"));
			}

			IBuildingMarkable::Execute_OnMarked(HitActor, HitResult.ImpactPoint, HitResult.ImpactNormal);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BUILDING_MARK] 360 Shot trace missed everything | Start=%s End=%s Length=%.0f cm. If building is farther than this, increase trace length. Otherwise check PropMesh collision profile / static mesh has collision geometry."),
			*ViewLocation.ToCompactString(), *TraceEnd.ToCompactString(), (TraceEnd - ViewLocation).Size());
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
