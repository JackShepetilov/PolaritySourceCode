// DamageType_Wallslam.h
// Damage type for wall slam impacts (kinetic category)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "DamageType_Wallslam.generated.h"

/**
 * Damage type used when NPCs are slammed into walls/surfaces
 * Part of the Kinetic damage category for damage number display
 */
UCLASS()
class POLARITY_API UDamageType_Wallslam : public UDamageType
{
	GENERATED_BODY()
};
