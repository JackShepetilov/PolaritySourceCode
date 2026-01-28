// DamageType_MomentumBonus.h
// Damage type for momentum bonus damage from melee attacks (kinetic category)

#pragma once

#include "CoreMinimal.h"
#include "DamageType_Melee.h"
#include "DamageType_MomentumBonus.generated.h"

/**
 * Damage type used for bonus damage from player momentum during melee attacks
 * Part of the Kinetic damage category for damage number display
 * Inherits from DamageType_Melee so it counts as melee for filtering purposes
 */
UCLASS()
class POLARITY_API UDamageType_MomentumBonus : public UDamageType_Melee
{
	GENERATED_BODY()
};
