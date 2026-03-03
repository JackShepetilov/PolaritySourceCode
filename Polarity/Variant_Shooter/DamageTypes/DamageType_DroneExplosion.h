// DamageType_DroneExplosion.h
// Damage type for flying drone explosion damage

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "DamageType_DroneExplosion.generated.h"

/**
 * Damage type used when a FlyingDrone explodes on death.
 * Used to identify drone explosion kills for health pickup drops.
 */
UCLASS()
class POLARITY_API UDamageType_DroneExplosion : public UDamageType
{
	GENERATED_BODY()
};
