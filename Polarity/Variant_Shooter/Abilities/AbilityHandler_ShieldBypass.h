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

	/** Best enemy by "central AND near", scored together.
	 *
	 *  Static and public because the aiming reticle has to run the SAME formula on the owning client
	 *  every frame while the key is held. Two copies of this scoring would drift, and the player
	 *  would watch brackets sit on one enemy while the bolt left for another. */
	static class AShooterNPC* ScoreBestTarget(const class AShooterCharacter* Caster, float Range);

protected:
	class AShooterNPC* FindTargetEnemy(float Range) const;
};
