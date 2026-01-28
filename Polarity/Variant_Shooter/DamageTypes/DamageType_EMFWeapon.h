// DamageType_EMFWeapon.h
// Damage type for EMF weapon damage (EMF category)

#pragma once

#include "CoreMinimal.h"
#include "DamageType_Ranged.h"
#include "DamageType_EMFWeapon.generated.h"

/**
 * Damage type used for EMF-charged projectile damage
 * Part of the EMF damage category for damage number display
 * Inherits from DamageType_Ranged so it counts as ranged for filtering purposes
 */
UCLASS()
class POLARITY_API UDamageType_EMFWeapon : public UDamageType_Ranged
{
	GENERATED_BODY()
};
