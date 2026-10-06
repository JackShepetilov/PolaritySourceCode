// AbilityHandler_NitroGate.h

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler_Throw.h"
#include "AbilityHandler_NitroGate.generated.h"

class ANitroGate;

/** Throws one gate per activation and keeps the caster's own gates inside the level's limit.
 *  Tap-or-hold, the montages and the release moment come from UAbilityHandler_Throw.
 *  Authority only, like every handler: TryActivate routes a client's press to the server first. */
UCLASS()
class POLARITY_API UAbilityHandler_NitroGate : public UAbilityHandler_Throw
{
	GENERATED_BODY()

protected:
	virtual void SpawnThrown(const FVector& Location, const FRotator& Rotation) override;

	/** Destroy the oldest of this caster's gates until at most Limit - 1 remain, so the one about to
	 *  be thrown fits. Compacts dead entries on the way through. */
	void MakeRoomForOneGate(int32 Limit);

	/** This caster's gates, oldest first. Weak: a gate can be shot, and the run can end under it. */
	TArray<TWeakObjectPtr<ANitroGate>> MyGates;
};
