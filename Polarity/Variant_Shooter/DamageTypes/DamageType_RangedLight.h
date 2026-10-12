// DamageType_RangedLight.h
// Damage type for rounds that are weak against body armour: pistols, SMGs

#pragma once

#include "CoreMinimal.h"
#include "DamageType_Ranged.h"
#include "DamageType_RangedLight.generated.h"

/**
 * A pistol or SMG round (Docs/Damage_Model_Rework_Plan_2026-10-03.md §8): full damage to a body,
 * less to its armour. Set it as the damage type of the weapon. The number is ArmorDamageScale.
 */
UCLASS()
class POLARITY_API UDamageType_RangedLight : public UDamageType_Ranged
{
	GENERATED_BODY()

public:

	UDamageType_RangedLight()
	{
		ArmorDamageScale = 0.6f;
	}
};
