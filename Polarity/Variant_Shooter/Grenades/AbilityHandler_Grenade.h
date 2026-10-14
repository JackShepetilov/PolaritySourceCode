// AbilityHandler_Grenade.h

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Abilities/AbilityHandler_Throw.h"
#include "AbilityHandler_Grenade.generated.h"

/**
 * Rides the throw pipeline and supplies the payload: one grenade out of the bag, in flight. Run
 * standalone by UGrenadeComponent (no ability slot, no cooldown). The grenade is taken from the bag
 * only when it leaves the hand, so a throw cancelled in the wind-up costs nothing.
 */
UCLASS()
class POLARITY_API UAbilityHandler_Grenade : public UAbilityHandler_Throw
{
	GENERATED_BODY()

protected:
	virtual void SpawnThrown(const FVector& Location, const FRotator& Rotation) override;
};
