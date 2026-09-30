// UpgradeDefinition_SlideFireFriction.cpp

#include "UpgradeDefinition_SlideFireFriction.h"

const FSlideFireFrictionLevelData& UUpgradeDefinition_SlideFireFriction::GetLevelData(int32 Level) const
{
	static const FSlideFireFrictionLevelData Fallback;
	if (LevelData.Num() == 0)
	{
		return Fallback;
	}
	return LevelData[FMath::Clamp(Level - 1, 0, LevelData.Num() - 1)];
}

#if WITH_EDITOR
void UUpgradeDefinition_SlideFireFriction::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MaxLevel = FMath::Max(1, LevelData.Num());
}
#endif
