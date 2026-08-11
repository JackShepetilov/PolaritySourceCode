// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_MaxHealth.h"
#include "UpgradeDefinition_MaxHealth.h"
#include "ShooterCharacter.h"

UUpgrade_MaxHealth::UUpgrade_MaxHealth()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgrade_MaxHealth::OnUpgradeActivated()
{
	CachedDef = Cast<UUpgradeDefinition_MaxHealth>(UpgradeDefinition);
	if (!CachedDef.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[MAX_HEALTH] Activation failed: UpgradeDefinition is not UUpgradeDefinition_MaxHealth"));
		return;
	}

	ApplyForLevel(CurrentLevel);
}

void UUpgrade_MaxHealth::OnUpgradeDeactivated()
{
	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		if (!FMath::IsNearlyZero(AppliedMaxHPBonus))
		{
			Character->ModifyMaxHP(-AppliedMaxHPBonus, false);
		}
	}

	AppliedMaxHPBonus = 0.0f;
	CachedDef.Reset();
}

void UUpgrade_MaxHealth::OnLevelChanged(int32 /*OldLevel*/, int32 NewLevel)
{
	ApplyForLevel(NewLevel);
}

void UUpgrade_MaxHealth::ApplyForLevel(int32 Level)
{
	if (!CachedDef.IsValid())
	{
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	const FMaxHealthLevelData& Data = CachedDef->GetLevelData(Level);
	const float NewBonus = FMath::Max(0.0f, Data.MaxHPBonus);
	const float DeltaBonus = NewBonus - AppliedMaxHPBonus;
	if (FMath::IsNearlyZero(DeltaBonus))
	{
		return;
	}

	Character->ModifyMaxHP(DeltaBonus, Data.bHealAddedMaxHP);
	AppliedMaxHPBonus = NewBonus;

	UE_LOG(LogTemp, Warning, TEXT("[MAX_HEALTH] Applied Lv%d bonus %.2f (delta %.2f), character max HP %.2f"),
		Level,
		AppliedMaxHPBonus,
		DeltaBonus,
		Character->GetMaxHP());
}
