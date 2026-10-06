// AbilityHandler_HealGrenade.h

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler_Throw.h"
#include "AbilityHandler_HealGrenade.generated.h"

/**
 * Rides the throw pipeline and supplies only the payload: montages, tap-or-hold, the release moment
 * and audio all come from UAbilityHandler_Throw.
 */
UCLASS()
class POLARITY_API UAbilityHandler_HealGrenade : public UAbilityHandler_Throw
{
	GENERATED_BODY()

protected:
	virtual void SpawnThrown(const FVector& Location, const FRotator& Rotation) override;
};
