// AbilityHandler_ShieldBypass.h

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler.h"
#include "AbilityHandler_ShieldBypass.generated.h"

/**
 * Runs the Wizard's active: pick the enemy being aimed at and open it up.
 *
 * Instant -- there is no cast to hold or interrupt, so it completes in the same call that starts it
 * and the component's cooldown begins straight away.
 *
 * This only ever runs on the authority. UAbilityComponent::TryActivate sends a client's press to the
 * server and returns, so by the time a handler's OnActivate is reached the machine deciding is the
 * one that owns the enemy's state. Handlers therefore contain no networking of their own, which was
 * the point of putting the ability system on the wire before writing any of them.
 */
UCLASS()
class POLARITY_API UAbilityHandler_ShieldBypass : public UAbilityHandler
{
	GENERATED_BODY()

public:
	virtual void OnActivate_Implementation() override;

protected:
	/** The enemy under the crosshair within Range, or null. Uses the character's aim rather than the
	 *  camera transform directly so it matches where the weapon shoots. */
	class AShooterNPC* FindTargetEnemy(float Range) const;
};
