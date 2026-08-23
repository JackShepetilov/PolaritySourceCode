// AbilityHandler_MeleePassive.h
// Answers one question, on every machine: how far may this swing reach for that enemy.

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler.h"
#include "AbilityHandler_MeleePassive.generated.h"

/**
 * Unlike every other handler in the project, this one is NOT authority-only.
 *
 * The lunge flies inside the movement simulation: the client that swings picks the target and flies
 * at it, and the server replays that same move and has to arrive at the same answer. A handler that
 * returned the extended reach on the server and the base reach on the client would produce a lunge
 * the client sees and the server rejects, and that reads in play as rubber-banding on every swing.
 *
 * So this has no state, does no work on equip, and is a pure function of the target: the same
 * inputs give the same number wherever it is asked. UAbilityComponent already builds a passive
 * handler on both sides (RebuildPassiveHandler runs from OnRep_PassiveDefinition too), which is
 * what makes that possible.
 *
 * The one gap left by that design is timing, not correctness: a client's PassiveDefinition arrives
 * by replication, and until it does that client's handler does not exist and its lunge is the base
 * one. The server's is too, because the server asks the same handler; they agree on the base value,
 * so the worst case is a short swing rather than a rejected one.
 */
UCLASS()
class POLARITY_API UAbilityHandler_MeleePassive : public UAbilityHandler
{
	GENERATED_BODY()

public:
	virtual float ModifyLungeRange(const AActor* Target, float BaseRange) const override;

protected:
	/** How much of this actor's shield is gone, 0 (untouched) to 1 (stripped).
	 *
	 *  Shield is the inverse of charge on the target's UEMFVelocityModifier -- zero charge is a
	 *  whole shield, MaxBaseCharge is none left -- which is the same reading the Tank's passive uses
	 *  for its damage return and the same instant IsAtMaxCharge() calls an enemy grabbable.
	 *
	 *  Anything with no such component reads as untouched rather than as stripped. That covers
	 *  teammates and training dummies, and it is the safe direction to be wrong in: the fallback is
	 *  the reach everybody already had. */
	static float GetShieldStrippedFraction(const AActor* Target);
};
