// AimPoints.h
// One call site for "what world point do I shoot at".

#pragma once

#include "CoreMinimal.h"

class AActor;

namespace PolarityAim
{
	/** World point to aim at, scatter included.
	 *
	 *  Asks the target through IAimTargetInterface; anything that does not implement it gets the
	 *  centre of its collision and a scatter proportional to its own height, which keeps unturned
	 *  props shootable without pretending they are human-shaped. */
	POLARITY_API FVector ResolveAimPoint(const AActor* Target);

	/** The same point without the random scatter, for code that needs a stable answer (a turret
	 *  tracking a target, a telegraph, a test). */
	POLARITY_API FVector ResolveAimPointExact(const AActor* Target);
}
