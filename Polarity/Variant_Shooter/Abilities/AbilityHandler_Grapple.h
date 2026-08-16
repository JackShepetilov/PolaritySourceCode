// AbilityHandler_Grapple.h

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler.h"
#include "AbilityHandler_Grapple.generated.h"

/** Traces where the caster is aiming and throws them at it. Authority only; the pull reaches the
 *  caster's own machine the same way a melee shove does. */
UCLASS()
class POLARITY_API UAbilityHandler_Grapple : public UAbilityHandler
{
	GENERATED_BODY()

public:
	virtual void OnActivate_Implementation() override;
};
