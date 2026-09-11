// KamikazeStrikeSubsystem.h
// Who each kamikaze drone flies at, where it hangs while it waits, and when it may strike.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "KamikazeStrikeSubsystem.generated.h"

class AKamikazeDroneNPC;

/**
 * Server-side bookkeeping for kamikaze drones, three jobs:
 *
 *  1. Targets are shared out EVENLY between the players: a drone goes to the player with the fewest
 *     drones on them (nearest on a tie) and only switches when that is two or more out of balance,
 *     so it does not flip between two players every frame.
 *  2. Each player's drones hang on a ring around them: one sector each, at alternating heights,
 *     so they neither stack on one point nor hide behind each other.
 *  3. Each player has a strike queue: at most polarity.kamikaze.maxstrikes drones in flight at them
 *     at once (default one), and polarity.kamikaze.strikegap seconds of rest after each strike ends.
 *     First come, first served; a drone that was shot jumps the queue.
 *
 * Only players are handed out here. A drone fighting another faction's pawn falls back to its own
 * target logic and never queues.
 */
UCLASS()
class POLARITY_API UKamikazeStrikeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	void Register(AKamikazeDroneNPC* Drone);

	/** Drops the drone from everything, and ends its strike if it had one. */
	void Unregister(AKamikazeDroneNPC* Drone);

	/** The player this drone should fly at, or null when there is no live hostile player. */
	APawn* GetAssignedTarget(AKamikazeDroneNPC* Drone);

	/** The drone's place on its target's ring: index among that target's drones, how many there are,
	 *  and the bearing the ring starts from (degrees). False when the drone is not on a ring. */
	bool GetHoldSlot(const AKamikazeDroneNPC* Drone, const APawn* Target, int32& OutIndex, int32& OutCount, float& OutBaseBearingDeg);

	/** Ask to strike Target now. True when granted; the drone must then start its strike. A granted
	 *  drone counts as in flight until EndStrike or Unregister. bPriority puts it at the front. */
	bool RequestStrike(AKamikazeDroneNPC* Drone, APawn* Target, bool bPriority);

	/** The strike is over (hit, miss or death): frees the slot and starts the rest gap. */
	void EndStrike(AKamikazeDroneNPC* Drone);

private:

	struct FTargetState
	{
		TArray<TWeakObjectPtr<AKamikazeDroneNPC>> Waiting;
		TArray<TWeakObjectPtr<AKamikazeDroneNPC>> Active;
		float LastStrikeEndTime = -100.0f;
		float BaseBearingDeg = 0.0f;
		bool bHasBase = false;
	};

	/** Registration order, which is also ring order. */
	TArray<TWeakObjectPtr<AKamikazeDroneNPC>> Drones;

	TMap<TWeakObjectPtr<AKamikazeDroneNPC>, TWeakObjectPtr<APawn>> Assignment;
	TMap<TWeakObjectPtr<APawn>, FTargetState> Targets;

	void Cleanup();
	int32 CountAssigned(const APawn* Target) const;
};
