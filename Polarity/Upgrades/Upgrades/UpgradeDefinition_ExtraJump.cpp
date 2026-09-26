// UpgradeDefinition_ExtraJump.cpp

#include "UpgradeDefinition_ExtraJump.h"

const FExtraJumpLevelData& UUpgradeDefinition_ExtraJump::GetLevelData(int32 Level) const
{
	static const FExtraJumpLevelData Fallback;
	if (LevelData.Num() == 0)
	{
		return Fallback;
	}
	return LevelData[FMath::Clamp(Level - 1, 0, LevelData.Num() - 1)];
}

#if WITH_EDITOR
void UUpgradeDefinition_ExtraJump::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MaxLevel = FMath::Max(1, LevelData.Num());
}
#endif
