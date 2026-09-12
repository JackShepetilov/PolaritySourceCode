// AbilityHandler_SmokeScreen.h

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler_Burst.h"
#include "AbilityHandler_SmokeScreen.generated.h"

/**
 * Rides the burst pipeline and replaces only the throw: montages, timing, spawn socket and audio all
 * come from UAbilityHandler_Burst.
 *
 * No target scoring, like the Wizard's puddle it is copied from: what the canister is aimed at is a
 * piece of floor.
 */
UCLASS()
class POLARITY_API UAbilityHandler_SmokeScreen : public UAbilityHandler_Burst
{
	GENERATED_BODY()

public:
	virtual void OnPerShotEffect_Implementation() override;
};
