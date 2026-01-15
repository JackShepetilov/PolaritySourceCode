// DamageType_Ranged.h
// Damage type for ranged attacks

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "DamageType_Ranged.generated.h"

/**
 * Damage type used for ranged attacks (bullets, projectiles, hitscan weapons)
 * Used by ShooterDummy to determine if damage should be accepted
 */
UCLASS()
class POLARITY_API UDamageType_Ranged : public UDamageType
{
	GENERATED_BODY()
};
