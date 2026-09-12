// AbilityHandler_SlowPuddle.h

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler_Burst.h"
#include "AbilityHandler_SlowPuddle.generated.h"

/**
 * Rides the burst pipeline and replaces only the shot: montages, timing, spawn socket and audio all
 * come from UAbilityHandler_Burst.
 *
 * There is no target scoring here, and its absence is the change. The ability it replaces picked the
 * best enemy and steered the bolt at it; this one throws where the player is looking, because what it
 * is aimed at is a piece of floor.
 */
UCLASS()
class POLARITY_API UAbilityHandler_SlowPuddle : public UAbilityHandler_Burst
{
	GENERATED_BODY()

public:
	virtual void OnPerShotEffect_Implementation() override;
};
