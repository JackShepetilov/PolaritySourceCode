// KamikazeStrikeSubsystem.h
// Who each kamikaze drone flies at, and when its strike may land.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "KamikazeStrikeSubsystem.generated.h"

class AKamikazeDroneNPC;

/**
 * Server-side bookkeeping for kamikaze drones, two jobs:
 *
 *  1. Targets are shared out EVENLY between the players: a drone goes to the player with the fewest
 *     drones on them (nearest on a tie) and only switches when that is two or more out of balance.
 *
 *  2. Strikes are SCHEDULED so the player can always answer every one of them. Drones may be in the
 *     air together; what is spaced out is when they land. Between two impacts on the same player
 *     there is at least
 *
 *         (time to kill + time to turn from the last drone to this one) * slack
 *
 *     plus, once per empty magazine, the weapon's reload time. The allowance is given once and comes
 *     back only after the player has actually reloaded, so an empty gun is not a shield.
 *     Order is first come, first served (the drone that got into position first strikes first);
 *     a drone that was shot goes to the front.
 *
 * Tuning (console): polarity.kamikaze.ttk, .turnspeed, .reaction, .slack, .killbullets.
 *
 * Only players are handed out here. A drone fighting another faction's pawn falls back to its own
 * target logic and is not scheduled.
 */
UCLASS()
class POLARITY_API UKamikazeStrikeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	void Register(AKamikazeDroneNPC* Drone);
	void Unregister(AKamikazeDroneNPC* Drone);

	/** The player this drone should fly at, or null when there is no live hostile player. */
	APawn* GetAssignedTarget(AKamikazeDroneNPC* Drone);

	/** Push that keeps a hold point clear of the other drones, so a group hangs as a loose cluster
	 *  instead of on one spot. Zero when nobody is close. */
	FVector GetSeparationOffset(const AKamikazeDroneNPC* Drone, const FVector& Point) const;

	/** Ask to strike Target now, given how long the strike takes to land. True when granted: the
	 *  drone must start its strike this frame. False: not its turn yet, or it would land too soon
	 *  after the previous one; ask again next frame. bPriority puts the drone at the front. */
	bool RequestStrike(AKamikazeDroneNPC* Drone, APawn* Target, bool bPriority, float FlightTime);

private:

	struct FTargetState
	{
		/** Drones ready to strike, in the order they became ready. */
		TArray<TWeakObjectPtr<AKamikazeDroneNPC>> Waiting;

		/** When the last granted strike was planned to land, and from which bearing it came. */
		float LastImpactTime = -100.0f;
		float LastStrikeBearingDeg = 0.0f;
		bool bHasLastStrike = false;

		/** The reload allowance has been given for the current empty magazine. */
		bool bReloadAllowanceUsed = false;
	};

	TArray<TWeakObjectPtr<AKamikazeDroneNPC>> Drones;
	TMap<TWeakObjectPtr<AKamikazeDroneNPC>, TWeakObjectPtr<APawn>> Assignment;
	TMap<TWeakObjectPtr<APawn>, FTargetState> Targets;

	void Cleanup();
	int32 CountAssigned(const APawn* Target) const;

	/** Reload time to add to the next gap, or zero. Re-arms the allowance once the magazine is fine
	 *  again; spending it is the grant's job, so a request that is refused spends nothing. */
	float PendingReloadAllowance(const APawn* Target, FTargetState& State) const;
};
