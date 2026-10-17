// RunLevelDefinition.cpp

#include "Variant_Shooter/Progression/RunLevelDefinition.h"

int32 URunLevelDefinition::GetLevelForXP(int32 XP) const
{
	int32 Level = 0;
	while (Steps.IsValidIndex(Level) && XP >= Steps[Level].XPToReach)
	{
		++Level;
	}
	return Level;
}

int32 URunLevelDefinition::GetXPToReach(int32 NextLevel) const
{
	return Steps.IsValidIndex(NextLevel - 1) ? Steps[NextLevel - 1].XPToReach : -1;
}

int32 URunLevelDefinition::GetGadgetsPerKind(int32 Level) const
{
	int32 Value = 0;
	for (int32 Index = 0; Index < Level && Steps.IsValidIndex(Index); ++Index)
	{
		if (Steps[Index].GadgetsPerKind > 0)
		{
			Value = Steps[Index].GadgetsPerKind;
		}
	}
	return Value;
}

float URunLevelDefinition::GetSuppressionResistance(int32 Level) const
{
	float Value = 0.0f;
	for (int32 Index = 0; Index < Level && Steps.IsValidIndex(Index); ++Index)
	{
		if (Steps[Index].SuppressionResistance >= 0.0f)
		{
			Value = FMath::Clamp(Steps[Index].SuppressionResistance, 0.0f, 1.0f);
		}
	}
	return Value;
}

const URunLevelDefinition* URunLevelSettings::GetDefinition()
{
	const URunLevelSettings* const Settings = GetDefault<URunLevelSettings>();
	return Settings ? Settings->Definition.LoadSynchronous() : nullptr;
}
