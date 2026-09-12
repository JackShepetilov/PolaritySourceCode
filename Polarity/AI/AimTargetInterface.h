// AimTargetInterface.h
// "Where should I be shot at", answered by the thing being shot at.
//
// The aim offsets used to live on the shooter: two numbers, -35 and -60, that describe the chest of
// a standing human and were applied to everything an NPC ever pointed a gun at. A shooter cannot
// know better - it sees an AActor* - while the target knows its own shape exactly. So the question
// moves to the target, and a shooter only asks.
//
// Anything that does not implement this is still shootable: the resolver falls back to the centre
// of the collision, which is the honest answer for a prop nobody has tuned.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "AimTargetInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UAimTargetInterface : public UInterface
{
	GENERATED_BODY()
};

class POLARITY_API IAimTargetInterface
{
	GENERATED_BODY()

public:

	/** Preferred aim point in the actor's own space, relative to its origin. Chest for a human,
	 *  the middle of the hull for a vehicle, the middle of the visible body for a drone. */
	virtual FVector GetLocalAimPoint() const = 0;

	/** Half range of the vertical scatter a shooter adds around that point, in cm. It is what makes
	 *  a burst walk over a body instead of stacking in one pixel, so it belongs to the target's size
	 *  as much as the point does. */
	virtual float GetAimVerticalJitter() const = 0;
};
