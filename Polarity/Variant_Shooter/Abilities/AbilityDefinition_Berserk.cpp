// AbilityDefinition_Berserk.cpp

#include "AbilityDefinition_Berserk.h"

FBerserkLevelStats UAbilityDefinition_Berserk::GetStatsAtLevel(int32 Level) const
{
	// An unauthored Levels array is a working ability, not an error: the struct's own defaults are
	// the tuning, and refusing to answer here would make an installed active behave like a missing
	// one with nothing in the log to explain it.
	if (Levels.Num() == 0)
	{
		return FBerserkLevelStats{};
	}
	return Levels[FMath::Clamp(Level - 1, 0, Levels.Num() - 1)];
}

FAbilityCommonStats UAbilityDefinition_Berserk::GetCommonStatsAtLevel(int32 Level) const
{
	const FBerserkLevelStats Stats = GetStatsAtLevel(Level);

	FAbilityCommonStats Common;
	Common.Cooldown = Stats.Cooldown;
	Common.MinimumChargeToActivate = Stats.MinimumChargeToActivate;
	return Common;
}
