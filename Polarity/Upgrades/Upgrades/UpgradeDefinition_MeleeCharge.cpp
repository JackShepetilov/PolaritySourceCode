// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "UpgradeDefinition_MeleeCharge.h"

const FMeleeChargeLevelData& UUpgradeDefinition_MeleeCharge::GetLevelData(int32 Level) const
{
	if (LevelData.Num() == 0)
	{
		static const FMeleeChargeLevelData DefaultData;
		return DefaultData;
	}

	const int32 Index = FMath::Clamp(Level - 1, 0, LevelData.Num() - 1);
	return LevelData[Index];
}

#if WITH_EDITOR
void UUpgradeDefinition_MeleeCharge::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MaxLevel = FMath::Max(1, LevelData.Num());
}
#endif
