// MeleeHoldAttack.h
// What an upgrade answers when asked "does holding the melee key do something of yours".

#pragma once

#include "CoreMinimal.h"

struct FMeleeAnimationData;

/**
 * A plain struct: it is filled and read inside one call and never travels.
 *
 * With an upgrade that answers, the melee key works like this: let go before HoldTime and the
 * ordinary swing comes out (on the release, so a tap is that much later than without the upgrade);
 * keep it down and the hold attack fires by itself the moment HoldTime is up.
 */
struct FMeleeHoldAttack
{
	/** Seconds the key has to stay down. */
	float HoldTime = 0.35f;

	/** The animation the hold attack plays instead of the ordinary one. Null = the ordinary one.
	 *  Points into the upgrade's own asset. */
	const FMeleeAnimationData* Animation = nullptr;

	/** What the hold prompt on the HUD calls it. The upgrade's own display name. */
	FText Label;
};
