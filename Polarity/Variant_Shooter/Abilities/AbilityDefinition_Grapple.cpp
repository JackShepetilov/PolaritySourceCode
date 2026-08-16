// AbilityDefinition_Grapple.cpp

#include "AbilityDefinition_Grapple.h"

FGrappleLevelStats UAbilityDefinition_Grapple::GetStatsAtLevel(int32 Level) const
{
	if (Levels.Num() == 0)
	{
		return FGrappleLevelStats{};
	}
	return Levels[FMath::Clamp(Level - 1, 0, Levels.Num() - 1)];
}

FAbilityCommonStats UAbilityDefinition_Grapple::GetCommonStatsAtLevel(int32 Level) const
{
	const FGrappleLevelStats Stats = GetStatsAtLevel(Level);
	FAbilityCommonStats Common;
	Common.Cooldown = Stats.Cooldown;
	Common.MinimumChargeToActivate = Stats.MinimumChargeToActivate;
	return Common;
}
