// AbilityDefinition_NitroGate.cpp

#include "AbilityDefinition_NitroGate.h"

FNitroGateLevelStats UAbilityDefinition_NitroGate::GetStatsAtLevel(int32 Level) const
{
	if (Levels.Num() == 0)
	{
		return FNitroGateLevelStats{};
	}
	return Levels[FMath::Clamp(Level - 1, 0, Levels.Num() - 1)];
}

FAbilityCommonStats UAbilityDefinition_NitroGate::GetCommonStatsAtLevel(int32 Level) const
{
	const FNitroGateLevelStats Stats = GetStatsAtLevel(Level);
	FAbilityCommonStats Common;
	Common.Cooldown = Stats.Cooldown;
	Common.MinimumChargeToActivate = Stats.MinimumChargeToActivate;
	return Common;
}
