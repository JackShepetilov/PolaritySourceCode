// PolarityTeams.h
// "Who fights whom", in one place.
//
// Until factions there was one answer and it was spelled as a tag: an actor carrying "Player" was
// the enemy, everybody else was not. That works exactly as long as there are two sides. A war
// between two AI factions and the players asks the question per asker, so it has to be a function
// of both actors rather than a property of one.
//
// The answer comes from the engine's own team attitude (IGenericTeamAgentInterface), which every
// APolarityCharacter and every AShooterAIController already implements. Nothing here invents a
// second notion of side; this is the one call site everybody else goes through, so that the rule
// can later grow (allies, neutrals, truces) in one edit instead of twenty.

#pragma once

#include "CoreMinimal.h"

class AActor;
class APawn;

namespace PolarityTeams
{
	/** The sides, spelled out once.
	 *
	 *  The same numbers APolarityCharacter::TeamByte and AShooterAIController::TeamId carry, so a
	 *  side and the ownership of a place on the map are one value and cannot drift apart. Neutral is
	 *  not a side: it is never given to a pawn, and only ever means "nobody". */
	constexpr uint8 Players = 0;
	constexpr uint8 FactionA = 1;
	constexpr uint8 FactionB = 2;
	constexpr uint8 Neutral = 255;

	/** True when A and B are on sides that fight each other.
	 *
	 *  Neutral, not hostile, is the answer for anything without a side: the engine's default
	 *  attitude returns Neutral when the other actor does not implement the team interface, so
	 *  props, pickups, decoy objects and world geometry stay out of target selection the same way
	 *  they stayed out of the old "Player" tag test.
	 *
	 *  Symmetric today (team 0 players, teams 1..N factions, everything different is hostile) but
	 *  deliberately takes both actors, because the moment one faction likes somebody the other one
	 *  shoots at, it stops being symmetric and every caller already asks the right question. */
	POLARITY_API bool AreHostile(const AActor* A, const AActor* B);

	/** Test switch behind `polarity.ai.IgnorePlayers`: while it is on, nobody's AI counts a player
	 *  as an enemy. It lives here rather than in one behaviour because "who is my enemy" is asked
	 *  from four unrelated places (perception, the controller's target intents, the combat
	 *  coordinator's group targets, plain hostility tests), and a switch that only covers some of
	 *  them looks broken: the NPCs stop shooting and the coordinator keeps herding them at you. */
	POLARITY_API bool ShouldIgnorePlayers();

	/** Team of an actor, resolved the way the engine resolves a stimulus source: through the actor
	 *  itself, with no fallback to its controller. 255 (FGenericTeamId::NoTeam) when it has none. */
	POLARITY_API uint8 GetTeam(const AActor* Actor);

	/** Every pawn in the world that Asker fights, players and other factions alike.
	 *
	 *  Walks all pawns rather than asking the coop helper: it finds both sides in one sweep, and it
	 *  does not depend on running where the whole team is visible (CoopPlayers::GetAll only sees the
	 *  local controller on a client). Hostility rejects everything else. */
	POLARITY_API void GatherHostilePawns(const AActor* Asker, TArray<APawn*>& OutPawns);

	/** Closest pawn Asker fights, or null. The fallback answer for "who am I acting against" when
	 *  nothing has assigned one; a system that has a real opinion (the coordinator, a squad order)
	 *  should be asked first, because this one has no memory and no hysteresis. */
	POLARITY_API APawn* FindNearestHostilePawn(const AActor* Asker);

	/** Who is actually responsible for a damage event, in the order the engine makes it available:
	 *  the instigator's pawn if there is one (a rifle's owner rather than the rifle), then the
	 *  causer's owner (a projectile's weapon's holder), then the causer itself (a thrown prop).
	 *
	 *  Damage arrives with three references and any of them can be the interesting one, so every
	 *  friendly-fire test used to spell out its own chain of casts. Null when nothing identifiable
	 *  is behind the hit, which is what world damage looks like. */
	POLARITY_API AActor* ResolveDamageSource(AActor* DamageCauser, AController* EventInstigator);
}
