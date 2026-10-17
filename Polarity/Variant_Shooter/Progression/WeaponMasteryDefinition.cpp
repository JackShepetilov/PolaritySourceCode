// WeaponMasteryDefinition.cpp

#include "Variant_Shooter/Progression/WeaponMasteryDefinition.h"

namespace
{
	// Every multiplier of the struct, so the resolve and the reset cannot miss one.
	float FWeaponMasteryHandling::* const MasteryHandlingFields[] =
	{
		&FWeaponMasteryHandling::ADSBlendInTimeMultiplier,
		&FWeaponMasteryHandling::ADSBlendOutTimeMultiplier,
		&FWeaponMasteryHandling::DrawDurationMultiplier,
		&FWeaponMasteryHandling::HolsterDurationMultiplier,
		&FWeaponMasteryHandling::SprintToFireTimeMultiplier,
		&FWeaponMasteryHandling::ReloadDurationMultiplier,
		&FWeaponMasteryHandling::SecondaryReloadTimeMultiplier,
		&FWeaponMasteryHandling::ReloadStartDurationMultiplier,
		&FWeaponMasteryHandling::ReloadLoopDurationMultiplier,
		&FWeaponMasteryHandling::ReloadEndDurationMultiplier,
		&FWeaponMasteryHandling::ADSFatigueRateMultiplier,
		&FWeaponMasteryHandling::ADSFatigueSwayMultiplier,
	};
}

FWeaponMasteryPerks::FWeaponMasteryPerks()
{
	for (float FWeaponMasteryHandling::* const Field : MasteryHandlingFields)
	{
		Handling.*Field = 1.0f;
	}
}

int32 UWeaponMasteryDefinition::GetLevelForXP(int32 XP) const
{
	int32 Level = 0;
	while (Steps.IsValidIndex(Level) && XP >= Steps[Level].XPToReach)
	{
		++Level;
	}
	return Level;
}

int32 UWeaponMasteryDefinition::GetXPToReach(int32 NextLevel) const
{
	return Steps.IsValidIndex(NextLevel - 1) ? Steps[NextLevel - 1].XPToReach : -1;
}

FWeaponMasteryPerks UWeaponMasteryDefinition::Resolve(int32 Level) const
{
	FWeaponMasteryPerks Perks;
	for (int32 Index = 0; Index < Level && Steps.IsValidIndex(Index); ++Index)
	{
		const FWeaponMasteryStep& Step = Steps[Index];

		// A time multiplier carries on until a later step sets it again; mastery never slows.
		for (float FWeaponMasteryHandling::* const Field : MasteryHandlingFields)
		{
			const float Value = Step.Handling.*Field;
			if (Value > 0.0f && FMath::IsFinite(Value))
			{
				Perks.Handling.*Field = FMath::Clamp(Value, 0.05f, 1.0f);
			}
		}

		// What a step opens stays open.
		Perks.bAllowADSWhileSliding |= Step.bAllowADSWhileSliding;
		Perks.bAllowADSWhileAirborne |= Step.bAllowADSWhileAirborne;
		Perks.bAllowReloadWhileADS |= Step.bAllowReloadWhileADS;
		Perks.bAllowReloadWhileSliding |= Step.bAllowReloadWhileSliding;
		Perks.bIgnoreADSMoveSpeedPenalty |= Step.bIgnoreADSMoveSpeedPenalty;
		Perks.bAllowTacticalSprint |= Step.bAllowTacticalSprint;
		Perks.bCanThrowWeapon |= Step.bCanThrowWeapon;
		if (Step.bFastSwapAfterKill)
		{
			Perks.bFastSwapAfterKill = true;
			Perks.FastSwapWindow = Step.FastSwapWindow;
			Perks.FastSwapSpeedMultiplier = FMath::Max(1.0f, Step.FastSwapSpeedMultiplier);
		}
	}
	return Perks;
}
