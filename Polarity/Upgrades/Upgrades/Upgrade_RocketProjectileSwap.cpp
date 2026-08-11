// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_RocketProjectileSwap.h"

#include "UpgradeDefinition_RocketProjectileSwap.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

UUpgrade_RocketProjectileSwap::UUpgrade_RocketProjectileSwap()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgrade_RocketProjectileSwap::OnUpgradeActivated()
{
	CachedDef = Cast<UUpgradeDefinition_RocketProjectileSwap>(UpgradeDefinition);
	if (!CachedDef.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[ROCKET_PROJECTILE_SWAP] UpgradeDefinition is not UUpgradeDefinition_RocketProjectileSwap"));
		return;
	}

	ApplyToWeapon(GetCurrentWeapon());
}

void UUpgrade_RocketProjectileSwap::OnUpgradeDeactivated()
{
	RestoreAllWeapons();
	CachedDef.Reset();
}

void UUpgrade_RocketProjectileSwap::OnLevelChanged(int32 OldLevel, int32 NewLevel)
{
	ApplyToWeapon(GetCurrentWeapon());

	UE_LOG(LogTemp, Warning, TEXT("[ROCKET_PROJECTILE_SWAP] Level %d -> %d"), OldLevel, NewLevel);
}

void UUpgrade_RocketProjectileSwap::OnWeaponChanged(AShooterWeapon* OldWeapon, AShooterWeapon* NewWeapon)
{
	RestoreWeapon(OldWeapon);
	ApplyToWeapon(NewWeapon);
}

void UUpgrade_RocketProjectileSwap::ApplyToWeapon(AShooterWeapon* Weapon)
{
	if (!CachedDef.IsValid() || !Weapon)
	{
		return;
	}

	const FRocketProjectileSwapLevelData& Data = CachedDef->GetLevelData(CurrentLevel);
	if (!IsEligibleWeapon(Weapon) || !Data.UpgradedProjectileClass)
	{
		RestoreWeapon(Weapon);
		return;
	}

	if (!OriginalProjectileClasses.Contains(Weapon))
	{
		OriginalProjectileClasses.Add(Weapon, Weapon->GetProjectileClass());
	}

	if (Weapon->GetProjectileClass() != Data.UpgradedProjectileClass)
	{
		Weapon->SetProjectileClass(Data.UpgradedProjectileClass);

		UE_LOG(LogTemp, Warning, TEXT("[ROCKET_PROJECTILE_SWAP] %s projectile class -> %s"),
			*GetNameSafe(Weapon),
			*GetNameSafe(Data.UpgradedProjectileClass.Get()));
	}
}

void UUpgrade_RocketProjectileSwap::RestoreWeapon(AShooterWeapon* Weapon)
{
	if (!Weapon)
	{
		return;
	}

	TSubclassOf<AShooterProjectile> OriginalClass;
	if (!OriginalProjectileClasses.RemoveAndCopyValue(Weapon, OriginalClass))
	{
		return;
	}

	if (IsValid(Weapon) && Weapon->GetProjectileClass() != OriginalClass)
	{
		Weapon->SetProjectileClass(OriginalClass);

		UE_LOG(LogTemp, Warning, TEXT("[ROCKET_PROJECTILE_SWAP] %s projectile class restored to %s"),
			*GetNameSafe(Weapon),
			*GetNameSafe(OriginalClass.Get()));
	}
}

void UUpgrade_RocketProjectileSwap::RestoreAllWeapons()
{
	TArray<AShooterWeapon*> Weapons;
	OriginalProjectileClasses.GetKeys(Weapons);

	for (AShooterWeapon* Weapon : Weapons)
	{
		RestoreWeapon(Weapon);
	}

	OriginalProjectileClasses.Empty();
}

bool UUpgrade_RocketProjectileSwap::IsEligibleWeapon(const AShooterWeapon* Weapon) const
{
	if (!Weapon || !CachedDef.IsValid())
	{
		return false;
	}

	const FRocketProjectileSwapLevelData& Data = CachedDef->GetLevelData(CurrentLevel);
	return Data.RequiredWeaponClass && Weapon->IsA(Data.RequiredWeaponClass);
}
