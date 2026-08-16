// AbilityHandler_ShieldLoan.h

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler.h"
#include "AbilityHandler_ShieldLoan.generated.h"

/**
 * Pledges a slice of the target's shield and dashes the caster at it.
 *
 * Authority only, like every handler: UAbilityComponent::TryActivate routes a client's press to the
 * server before any of this runs. The dash is the one part that has to reach the caster's own
 * machine as well, and it does that through the same road a melee shove already uses.
 */
UCLASS()
class POLARITY_API UAbilityHandler_ShieldLoan : public UAbilityHandler
{
	GENERATED_BODY()

public:
	virtual void OnActivate_Implementation() override;

protected:
	class AShooterNPC* FindTargetEnemy(float Range) const;
};
