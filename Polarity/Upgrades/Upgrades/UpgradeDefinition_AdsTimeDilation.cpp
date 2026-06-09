// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "UpgradeDefinition_AdsTimeDilation.h"

UUpgradeDefinition_AdsTimeDilation::UUpgradeDefinition_AdsTimeDilation()
{
	// This upgrade consumes the shared stored-health-pickup pool. Setting it here drives the HUD
	// heal-charge entry and lets the designer rely on it without ticking a checkbox per asset.
	bUsesStoredHealthPickups = true;

	// Ship with one Lv 1 entry so a freshly-created DataAsset works out of the box.
	if (LevelData.Num() == 0)
	{
		LevelData.Add(FAdsTimeDilationLevelData());
	}
}

const FAdsTimeDilationLevelData& UUpgradeDefinition_AdsTimeDilation::GetLevelData(int32 Level) const
{
	static const FAdsTimeDilationLevelData FallbackEmpty;
	if (LevelData.Num() == 0)
	{
		return FallbackEmpty;
	}
	const int32 Idx = FMath::Clamp(Level - 1, 0, LevelData.Num() - 1);
	return LevelData[Idx];
}

#if WITH_EDITOR
void UUpgradeDefinition_AdsTimeDilation::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// MaxLevel always tracks the number of authored level entries.
	const int32 NewMaxLevel = FMath::Max(1, LevelData.Num());
	if (MaxLevel != NewMaxLevel)
	{
		MaxLevel = NewMaxLevel;
	}
}
#endif
