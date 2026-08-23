// AbilityDefinition_SniperPassive.cpp

#include "AbilityDefinition_SniperPassive.h"

FSniperPassiveLevelStats UAbilityDefinition_SniperPassive::GetStatsAtLevel(int32 Level) const
{
	// An unauthored Levels array is not an error, the same way it is not one on the other two
	// passives: the struct's defaults are a working passive, and returning nothing here would make
	// an installed passive behave like a missing one with nothing in the log to say so.
	if (Levels.Num() == 0)
	{
		return FSniperPassiveLevelStats{};
	}
	return Levels[FMath::Clamp(Level - 1, 0, Levels.Num() - 1)];
}
