// CoopPlayers.h
// The team, instead of "the player".
//
// Single player let every system reach for UGameplayStatics::GetPlayerPawn(World, 0). Coop has no
// such thing: there are up to four pawns, and player 0 is whoever happened to join first. These
// helpers are the replacement, and they are deliberately dumb - no caching, no aggro policy, no
// state. Anything that needs to remember a target should hold its own weak pointer.

#pragma once

#include "CoreMinimal.h"

class AActor;
class APawn;
class APlayerController;
class UWorld;

namespace CoopPlayers
{
	/** Every player pawn in the world, in player-controller order.
	 *  Server-side this is the whole team. On a client the engine only iterates the local
	 *  controller, so callers that must see everyone have to run on the server. */
	POLARITY_API void GetAll(const UWorld* World, TArray<APawn*>& OutPawns);

	/** Player pawn closest to Location, or null when the team is empty.
	 *  Does not filter downed players: there is no downed state yet. */
	POLARITY_API APawn* GetNearest(const UWorld* World, const FVector& Location);

	/** True when Actor is a pawn a human is driving. */
	POLARITY_API bool IsPlayer(const AActor* Actor);

	/** The controller sitting at THIS machine. Use only for things that are local by nature:
	 *  HUD, subtitles, camera and display settings, console commands. Never for gameplay, where
	 *  "the local player" on the host silently means "the host" for everyone else.
	 *  Unlike UWorld::GetFirstPlayerController this says locality out loud. */
	POLARITY_API APlayerController* GetLocalController(const UWorld* World);
}
