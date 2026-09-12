// TacticalSpace.h
// "Is this a good piece of ground to stand on", answered the way Killzone answers it.
//
// The problem it solves: every position picker in this project scores a candidate against the
// TARGET alone (line of sight, cover, distance to fire from). Nothing in that scoring knows the
// candidate has friends or that it is about to plant itself inside the enemy formation, so with
// several units picking independently the two sides grow through each other. No line, no flanks,
// nothing to hold.
//
// The fix is the one every tactical shooter uses: a position evaluation function with a term for
// friendly presence and a term for hostile presence. Allies contribute positive influence with a
// distance falloff, enemies negative. A spot with lots of own influence and little enemy influence
// is your own ground; where the two are equal is the contact line. Nobody draws that line, it comes
// out of the arithmetic, and squads clump on their own side of it because that is where the score
// is highest.
//
// This is the grid-free version: it sums over the handful of pawns actually in play instead of
// maintaining an influence map. Same terms, same behaviour, and when a real grid arrives it can
// feed these two numbers instead of the loops here, without touching a single caller.

#pragma once

#include "CoreMinimal.h"

class AActor;
class APawn;

namespace TacticalSpace
{
	/** How much a unit cares about the two influences. Tuned per behaviour: a drone holding a firing
	 *  line wants distance from the enemy, a pushing melee unit wants the opposite. */
	struct FSpaceWeights
	{
		/** Pull toward friendly presence */
		float AllyWeight = 1.0f;

		/** Push away from hostile presence. Negative while pushing: closing IS the job. */
		float EnemyWeight = 1.0f;

		/** Distance to a teammate that scores best. Close enough to support, far enough not to share
		 *  one grenade. */
		float IdealAllySpacing = 700.0f;

		/** How far off IdealAllySpacing a teammate can be before it stops counting */
		float AllyTolerance = 1400.0f;

		/** Inside this an enemy is unpleasant to stand next to, and it gets worse linearly */
		float EnemyFalloff = 2000.0f;
	};

	/** The pawns that matter for one pick. Built once per pick, not per candidate: gathering is the
	 *  expensive half and the candidate loop is the cheap half. */
	struct FSpaceContext
	{
		TArray<FVector> Allies;
		TArray<FVector> Enemies;
	};

	/** Collect who is around Asker, split by hostility (PolarityTeams decides, so a truce or the
	 *  ignore-players switch is honoured automatically). Reads the combat coordinator's registry
	 *  plus the players rather than sweeping the world. */
	POLARITY_API void BuildContext(const AActor* Asker, FSpaceContext& OutContext);

	/** Score for standing at Candidate. Positive is good ground, negative is somebody else's.
	 *  Roughly in [-1, +1] per term, so callers can weigh it against their own criteria. */
	POLARITY_API float ScorePosition(const FSpaceContext& Context, const FVector& Candidate, const FSpaceWeights& Weights);
}
