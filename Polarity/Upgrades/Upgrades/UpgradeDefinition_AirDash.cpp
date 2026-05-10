// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "UpgradeDefinition_AirDash.h"

const FAirDashLevelData& UUpgradeDefinition_AirDash::GetLevelData(int32 Level) const
{
	static const FAirDashLevelData FallbackEmpty;
	if (LevelData.Num() == 0)
	{
		return FallbackEmpty;
	}
	const int32 Idx = FMath::Clamp(Level - 1, 0, LevelData.Num() - 1);
	return LevelData[Idx];
}

#if WITH_EDITOR
void UUpgradeDefinition_AirDash::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Auto-sync base MaxLevel to the length of LevelData so designers don't have to update both.
	const int32 NewMaxLevel = FMath::Max(1, LevelData.Num());
	if (MaxLevel != NewMaxLevel)
	{
		MaxLevel = NewMaxLevel;
	}
}
#endif
