// AbilityDefinition_TankPassive.cpp

#include "AbilityDefinition_TankPassive.h"

FTankPassiveLevelStats UAbilityDefinition_TankPassive::GetStatsAtLevel(int32 Level) const
{
	// An unauthored Levels array is not an error here the way it would be for an active: the struct's
	// defaults are a working passive, and a passive with no numbers would otherwise be a silent
	// nothing that looks installed.
	if (Levels.Num() == 0)
	{
		return FTankPassiveLevelStats{};
	}
	return Levels[FMath::Clamp(Level - 1, 0, Levels.Num() - 1)];
}
