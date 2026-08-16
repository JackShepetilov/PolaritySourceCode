// AbilityHandler_ShieldBypass.h

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler_Burst.h"
#include "AbilityHandler_ShieldBypass.generated.h"

/**
 * Rides the burst pipeline and replaces only the shot itself: the montages, the timing, the spawn
 * socket and the audio all come from UAbilityHandler_Burst, and this puts a homing bolt where a
 * burst projectile would have gone.
 */
UCLASS()
class POLARITY_API UAbilityHandler_ShieldBypass : public UAbilityHandler_Burst
{
	GENERATED_BODY()

public:
	virtual void OnPerShotEffect_Implementation() override;

protected:
	/** Best enemy by "central AND near", scored together. */
	class AShooterNPC* FindTargetEnemy(float Range) const;
};
