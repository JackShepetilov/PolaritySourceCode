// UpgradeDefinition_ChargedJump.cpp

#include "UpgradeDefinition_ChargedJump.h"

const FChargedJumpLevelData& UUpgradeDefinition_ChargedJump::GetLevelData(int32 Level) const
{
	static const FChargedJumpLevelData Fallback;
	if (LevelData.Num() == 0)
	{
		return Fallback;
	}
	return LevelData[FMath::Clamp(Level - 1, 0, LevelData.Num() - 1)];
}

#if WITH_EDITOR
void UUpgradeDefinition_ChargedJump::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MaxLevel = FMath::Max(1, LevelData.Num());
}
#endif
