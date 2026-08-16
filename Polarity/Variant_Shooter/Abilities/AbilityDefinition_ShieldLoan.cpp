// AbilityDefinition_ShieldLoan.cpp

#include "AbilityDefinition_ShieldLoan.h"

FShieldLoanLevelStats UAbilityDefinition_ShieldLoan::GetStatsAtLevel(int32 Level) const
{
	if (Levels.Num() == 0)
	{
		return FShieldLoanLevelStats{};
	}
	return Levels[FMath::Clamp(Level - 1, 0, Levels.Num() - 1)];
}

FAbilityCommonStats UAbilityDefinition_ShieldLoan::GetCommonStatsAtLevel(int32 Level) const
{
	const FShieldLoanLevelStats Stats = GetStatsAtLevel(Level);
	FAbilityCommonStats Common;
	Common.Cooldown = Stats.Cooldown;
	Common.MinimumChargeToActivate = Stats.MinimumChargeToActivate;
	return Common;
}
