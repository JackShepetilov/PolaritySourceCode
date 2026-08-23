// AbilityHandler_MeleePassive.cpp

#include "AbilityHandler_MeleePassive.h"
#include "AbilityDefinition_MeleePassive.h"
#include "EMFVelocityModifier.h"
#include "GameFramework/Actor.h"

float UAbilityHandler_MeleePassive::GetShieldStrippedFraction(const AActor* Target)
{
	if (!Target)
	{
		return 0.0f;
	}

	const UEMFVelocityModifier* Mod = Target->FindComponentByClass<UEMFVelocityModifier>();
	if (!Mod || Mod->MaxBaseCharge <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	// Magnitude, not the signed value: a negatively electrified enemy has had exactly as much shield
	// taken off it as a positive one, and the sign is only which way it was pushed.
	return FMath::Clamp(FMath::Abs(Mod->GetCharge()) / Mod->MaxBaseCharge, 0.0f, 1.0f);
}

float UAbilityHandler_MeleePassive::ModifyLungeRange(const AActor* Target, float BaseRange) const
{
	const UAbilityDefinition_MeleePassive* Def = Cast<UAbilityDefinition_MeleePassive>(GetDefinition());
	if (!Def || BaseRange <= 0.0f)
	{
		return BaseRange;
	}

	const FMeleePassiveLevelStats Stats = Def->GetStatsAtLevel(GetCurrentLevel());

	// Clamped against each other rather than trusted: an author who types the smaller number into
	// the second field gets a passive that shortens the lunge on a broken shield, which is the exact
	// opposite of the mechanic and would be read as the passive not working at all.
	const float MinMultiplier = FMath::Max(0.0f, Stats.ReachMultiplierAtFullShield);
	const float MaxMultiplier = FMath::Max(MinMultiplier, Stats.ReachMultiplierAtNoShield);

	// No candidate means the caller is sizing its search sphere and is asking for the ceiling: the
	// furthest this passive could ever let a swing reach. Handing back the base range here would be
	// worse than useless -- the search would never overlap the enemy the extended reach exists for,
	// so the filter that follows would have nothing to accept.
	if (!Target)
	{
		return BaseRange * MaxMultiplier;
	}

	return BaseRange * FMath::Lerp(MinMultiplier, MaxMultiplier, GetShieldStrippedFraction(Target));
}
