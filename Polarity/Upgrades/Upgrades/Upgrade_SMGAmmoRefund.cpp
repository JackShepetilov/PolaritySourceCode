// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_SMGAmmoRefund.h"
#include "UpgradeDefinition_SMGAmmoRefund.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

UUpgrade_SMGAmmoRefund::UUpgrade_SMGAmmoRefund()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgrade_SMGAmmoRefund::OnUpgradeActivated()
{
	CachedDef = Cast<UUpgradeDefinition_SMGAmmoRefund>(UpgradeDefinition);
	if (!CachedDef.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[SMG_AMMO_REFUND] UpgradeDefinition is not UUpgradeDefinition_SMGAmmoRefund"));
	}
}

void UUpgrade_SMGAmmoRefund::OnWeaponDealtDamage(AShooterWeapon* Weapon, AActor* /*Target*/, float Damage, bool /*bKilled*/)
{
	if (!CachedDef.IsValid() || !Weapon || Damage <= 0.0f || !IsEligibleWeapon(Weapon))
	{
		return;
	}

	const FSMGAmmoRefundLevelData& Data = CachedDef->GetLevelData(CurrentLevel);
	if (Data.RefundChance <= 0.0f || Data.AmmoToRefund <= 0)
	{
		return;
	}

	const int32 MagazineSize = Weapon->GetMagazineSize();
	const int32 CurrentAmmo = Weapon->GetBulletCount();
	if (MagazineSize <= 0 || CurrentAmmo >= MagazineSize)
	{
		return;
	}

	if (FMath::FRand() > Data.RefundChance)
	{
		return;
	}

	Weapon->SetBulletCount(CurrentAmmo + Data.AmmoToRefund);

	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		if (Character->GetCurrentWeapon() == Weapon)
		{
			Character->UpdateWeaponHUD(Weapon->GetBulletCount(), MagazineSize);
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("[SMG_AMMO_REFUND] Refunded %d ammo to %s (%d/%d)"),
		Data.AmmoToRefund,
		*GetNameSafe(Weapon),
		Weapon->GetBulletCount(),
		MagazineSize);
}

bool UUpgrade_SMGAmmoRefund::IsEligibleWeapon(const AShooterWeapon* Weapon) const
{
	if (!Weapon || !CachedDef.IsValid())
	{
		return false;
	}

	const FSMGAmmoRefundLevelData& Data = CachedDef->GetLevelData(CurrentLevel);
	return !Data.RequiredWeaponClass || Weapon->IsA(Data.RequiredWeaponClass);
}
