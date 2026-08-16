// AbilityDefinition_ShieldRestore.cpp

#include "AbilityDefinition_ShieldRestore.h"

FShieldRestoreLevelStats UAbilityDefinition_ShieldRestore::GetStatsAtLevel(int32 Level) const
{
	if (Levels.Num() == 0)
	{
		return FShieldRestoreLevelStats{};
	}
	return Levels[FMath::Clamp(Level - 1, 0, Levels.Num() - 1)];
}

FAbilityCommonStats UAbilityDefinition_ShieldRestore::GetCommonStatsAtLevel(int32 Level) const
{
	const FShieldRestoreLevelStats Stats = GetStatsAtLevel(Level);
	FAbilityCommonStats Common;
	Common.Cooldown = Stats.Cooldown;
	Common.MinimumChargeToActivate = Stats.MinimumChargeToActivate;
	return Common;
}
