// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "UpgradeDefinition_SwordSlide.h"

const FSwordSlideLevelData& UUpgradeDefinition_SwordSlide::GetLevelData(int32 Level) const
{
	static const FSwordSlideLevelData Fallback;
	if (LevelData.Num() == 0)
	{
		return Fallback;
	}
	return LevelData[FMath::Clamp(Level - 1, 0, LevelData.Num() - 1)];
}

#if WITH_EDITOR
void UUpgradeDefinition_SwordSlide::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MaxLevel = FMath::Max(1, LevelData.Num());
}
#endif
