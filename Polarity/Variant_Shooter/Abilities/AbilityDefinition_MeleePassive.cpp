// AbilityDefinition_MeleePassive.cpp

#include "AbilityDefinition_MeleePassive.h"

FMeleePassiveLevelStats UAbilityDefinition_MeleePassive::GetStatsAtLevel(int32 Level) const
{
	// An unauthored Levels array is not an error, for the same reason it is not one on the Tank's
	// passive: the struct's defaults are a working passive, and returning nothing here would make an
	// installed passive behave like a missing one with nothing in the log to say so.
	if (Levels.Num() == 0)
	{
		return FMeleePassiveLevelStats{};
	}
	return Levels[FMath::Clamp(Level - 1, 0, Levels.Num() - 1)];
}
