// AbilityHandler_EMFBurst.h
// Concrete burst-archetype ability: spawns one AEMFProjectile per per-shot AnimNotify.

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler_Burst.h"
#include "AbilityHandler_EMFBurst.generated.h"

UCLASS()
class POLARITY_API UAbilityHandler_EMFBurst : public UAbilityHandler_Burst
{
	GENERATED_BODY()

public:
	virtual void OnPerShotEffect_Implementation() override;
};
