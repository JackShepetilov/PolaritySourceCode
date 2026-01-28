// DamageType_EMFProximity.h
// Damage type for EMF proximity damage (EMF category)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "DamageType_EMFProximity.generated.h"

/**
 * Damage type used when opposite-charged NPCs collide due to EMF attraction
 * Part of the EMF damage category for damage number display
 */
UCLASS()
class POLARITY_API UDamageType_EMFProximity : public UDamageType
{
	GENERATED_BODY()
};
