// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_DropChargeOverride.h"
#include "UpgradeDefinition_DropChargeOverride.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"

UUpgrade_DropChargeOverride::UUpgrade_DropChargeOverride()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgrade_DropChargeOverride::OnUpgradeActivated()
{
	CachedDef = Cast<UUpgradeDefinition_DropChargeOverride>(UpgradeDefinition);
	if (!CachedDef.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[DROP_CHARGE_OVERRIDE] UpgradeDefinition is not UUpgradeDefinition_DropChargeOverride"));
	}
}

void UUpgrade_DropChargeOverride::OnEnemyDroppedRangedWeapon(ADroppedRangedWeapon* DroppedWeapon, AActor* DroppingEnemy)
{
	if (!CachedDef.IsValid() || !DroppedWeapon)
	{
		return;
	}

	const FDropChargeOverrideLevelData& Data = CachedDef->GetLevelData(CurrentLevel);
	const float Threshold = FMath::Max(0.0f, Data.MaxAbsChargeToOverride);
	const float OldCharge = DroppedWeapon->GetCharge();

	if (FMath::Abs(OldCharge) > Threshold)
	{
		return;
	}

	DroppedWeapon->SetCharge(Data.ReplacementCharge);

	UE_LOG(LogTemp, Warning, TEXT("[DROP_CHARGE_OVERRIDE] %s from %s charge %.2f -> %.2f (threshold abs <= %.2f)"),
		*GetNameSafe(DroppedWeapon),
		*GetNameSafe(DroppingEnemy),
		OldCharge,
		DroppedWeapon->GetCharge(),
		Threshold);
}
