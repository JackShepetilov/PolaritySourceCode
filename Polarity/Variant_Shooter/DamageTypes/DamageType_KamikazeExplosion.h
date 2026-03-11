// DamageType_KamikazeExplosion.h
// Damage type for kamikaze drone explosion damage (collision with player or crash)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "DamageType_KamikazeExplosion.generated.h"

/**
 * Damage type used when a KamikazeDroneNPC explodes.
 * Separate from DamageType_DroneExplosion to distinguish kamikaze crash (no HP pickup)
 * from intentional kill (HP pickup drops).
 */
UCLASS()
class POLARITY_API UDamageType_KamikazeExplosion : public UDamageType
{
	GENERATED_BODY()
};
