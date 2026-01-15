// DamageType_Melee.h
// Damage type for melee attacks

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "DamageType_Melee.generated.h"

/**
 * Damage type used for melee attacks (punches, kicks, melee weapons)
 * Used by ShooterDummy to determine if damage should be accepted
 */
UCLASS()
class POLARITY_API UDamageType_Melee : public UDamageType
{
	GENERATED_BODY()
};
