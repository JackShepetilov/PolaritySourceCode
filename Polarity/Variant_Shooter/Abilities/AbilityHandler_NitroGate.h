// AbilityHandler_NitroGate.h

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler.h"
#include "AbilityHandler_NitroGate.generated.h"

class ANitroGate;

/** Throws one gate per activation and keeps the caster's own gates inside the level's limit.
 *  Authority only, like every handler: TryActivate routes a client's press to the server first. */
UCLASS()
class POLARITY_API UAbilityHandler_NitroGate : public UAbilityHandler
{
	GENERATED_BODY()

public:
	virtual void OnActivate_Implementation() override;

protected:
	/** Destroy the oldest of this caster's gates until at most Limit - 1 remain, so the one about to
	 *  be thrown fits. Compacts dead entries on the way through. */
	void MakeRoomForOneGate(int32 Limit);

	/** This caster's gates, oldest first. Weak: a gate can be shot, and the run can end under it. */
	TArray<TWeakObjectPtr<ANitroGate>> MyGates;
};
