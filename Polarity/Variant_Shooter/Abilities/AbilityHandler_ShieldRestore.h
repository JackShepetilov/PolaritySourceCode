// AbilityHandler_ShieldRestore.h

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler.h"
#include "AbilityHandler_ShieldRestore.generated.h"

/** Hands an enemy its shield back and heals the caster for what was handed over. Authority only,
 *  like every handler: TryActivate routes a client's press to the server before this runs. */
UCLASS()
class POLARITY_API UAbilityHandler_ShieldRestore : public UAbilityHandler
{
	GENERATED_BODY()

public:
	virtual void OnActivate_Implementation() override;

protected:
	class AShooterNPC* FindTargetEnemy(float Range) const;
};
