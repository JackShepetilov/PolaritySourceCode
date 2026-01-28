// DamageType_Dropkick.h
// Damage type for dropkick attacks (kinetic category)

#pragma once

#include "CoreMinimal.h"
#include "DamageType_Melee.h"
#include "DamageType_Dropkick.generated.h"

/**
 * Damage type used for dropkick bonus damage (airborne downward attacks)
 * Part of the Kinetic damage category for damage number display
 * Inherits from DamageType_Melee so it counts as melee for filtering purposes
 */
UCLASS()
class POLARITY_API UDamageType_Dropkick : public UDamageType_Melee
{
	GENERATED_BODY()
};
