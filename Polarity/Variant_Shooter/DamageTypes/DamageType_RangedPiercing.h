// DamageType_RangedPiercing.h
// Damage type for rounds that are strong against body armour: sniper rifles, machine guns

#pragma once

#include "CoreMinimal.h"
#include "DamageType_Ranged.h"
#include "DamageType_RangedPiercing.generated.h"

/**
 * A sniper or machine gun round (Docs/Damage_Model_Rework_Plan_2026-10-03.md §8): full damage to a
 * body, more to its armour. Set it as the damage type of the weapon. The number is ArmorDamageScale.
 */
UCLASS()
class POLARITY_API UDamageType_RangedPiercing : public UDamageType_Ranged
{
	GENERATED_BODY()

public:

	UDamageType_RangedPiercing()
	{
		ArmorDamageScale = 1.5f;
	}
};
