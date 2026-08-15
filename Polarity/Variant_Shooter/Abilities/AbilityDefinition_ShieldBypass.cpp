// AbilityDefinition_ShieldBypass.cpp

#include "AbilityDefinition_ShieldBypass.h"

FShieldBypassLevelStats UAbilityDefinition_ShieldBypass::GetStatsAtLevel(int32 Level) const
{
	if (Levels.Num() == 0)
	{
		return FShieldBypassLevelStats{};
	}
	const int32 Index = FMath::Clamp(Level - 1, 0, Levels.Num() - 1);
	return Levels[Index];
}

FAbilityCommonStats UAbilityDefinition_ShieldBypass::GetCommonStatsAtLevel(int32 Level) const
{
	const FShieldBypassLevelStats Stats = GetStatsAtLevel(Level);

	FAbilityCommonStats Common;
	Common.Cooldown = Stats.Cooldown;
	Common.MinimumChargeToActivate = Stats.MinimumChargeToActivate;
	return Common;
}
