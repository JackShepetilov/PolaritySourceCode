// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_VerticalCooling.h"
#include "UpgradeDefinition_VerticalCooling.h"
#include "ShooterCharacter.h"

UUpgrade_VerticalCooling::UUpgrade_VerticalCooling()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UUpgrade_VerticalCooling::OnUpgradeActivated()
{
	CachedDef = Cast<UUpgradeDefinition_VerticalCooling>(UpgradeDefinition);
	if (!CachedDef.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[VERTICAL_COOLING] Activation failed: UpgradeDefinition is not UUpgradeDefinition_VerticalCooling"));
		return;
	}

	RemainingHealPool = CachedDef->MaxHealPool;
	PoolRefreshElapsed = 0.0f;
	bHasPreviousZ = false;

	SetComponentTickEnabled(true);

	UE_LOG(LogTemp, Warning, TEXT("[VERTICAL_COOLING] Activated: %.2f HP/m, pool %.2f HP, refresh %.2fs"),
		CachedDef->HealPerMeter,
		CachedDef->MaxHealPool,
		CachedDef->PoolRefreshInterval);
}

void UUpgrade_VerticalCooling::OnUpgradeDeactivated()
{
	SetComponentTickEnabled(false);

	CachedDef.Reset();
	RemainingHealPool = 0.0f;
	PoolRefreshElapsed = 0.0f;
	bHasPreviousZ = false;
}

void UUpgrade_VerticalCooling::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CachedDef.IsValid())
	{
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		bHasPreviousZ = false;
		return;
	}

	const float RefreshInterval = FMath::Max(CachedDef->PoolRefreshInterval, KINDA_SMALL_NUMBER);
	PoolRefreshElapsed += DeltaTime;
	while (PoolRefreshElapsed >= RefreshInterval)
	{
		PoolRefreshElapsed -= RefreshInterval;
		RefreshHealPool();
	}

	const float CurrentZ = Character->GetActorLocation().Z;
	if (!bHasPreviousZ)
	{
		PreviousZ = CurrentZ;
		bHasPreviousZ = true;
		return;
	}

	const float DeltaZ = FMath::Abs(CurrentZ - PreviousZ);
	PreviousZ = CurrentZ;

	if (DeltaZ < CachedDef->MinVerticalDeltaToCount ||
		RemainingHealPool <= 0.0f ||
		CachedDef->HealPerMeter <= 0.0f ||
		Character->IsDead())
	{
		return;
	}

	const float MissingHP = Character->GetMaxHP() - Character->GetCurrentHP();
	if (MissingHP <= 0.0f)
	{
		return;
	}

	const float VerticalMeters = DeltaZ / 100.0f;
	const float RequestedHeal = VerticalMeters * CachedDef->HealPerMeter;
	const float AppliedHeal = FMath::Min(RequestedHeal, FMath::Min(RemainingHealPool, MissingHP));
	if (AppliedHeal <= 0.0f)
	{
		return;
	}

	Character->RestoreHealth(AppliedHeal);
	RemainingHealPool = FMath::Max(0.0f, RemainingHealPool - AppliedHeal);
}

void UUpgrade_VerticalCooling::RefreshHealPool()
{
	if (!CachedDef.IsValid())
	{
		return;
	}

	RemainingHealPool = CachedDef->MaxHealPool;
}
