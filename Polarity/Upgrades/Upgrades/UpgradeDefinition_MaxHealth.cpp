// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "UpgradeDefinition_MaxHealth.h"

const FMaxHealthLevelData& UUpgradeDefinition_MaxHealth::GetLevelData(int32 Level) const
{
	static const FMaxHealthLevelData FallbackEmpty;
	if (LevelData.Num() == 0)
	{
		return FallbackEmpty;
	}

	const int32 Idx = FMath::Clamp(Level - 1, 0, LevelData.Num() - 1);
	return LevelData[Idx];
}

#if WITH_EDITOR
void UUpgradeDefinition_MaxHealth::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const int32 NewMaxLevel = FMath::Max(1, LevelData.Num());
	if (MaxLevel != NewMaxLevel)
	{
		MaxLevel = NewMaxLevel;
	}
}
#endif
