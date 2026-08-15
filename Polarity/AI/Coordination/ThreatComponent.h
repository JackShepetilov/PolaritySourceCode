// ThreatComponent.h
// How loudly a player is asking to be attacked, right now.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ThreatComponent.generated.h"

/**
 * A player's current threat: not a running total for the fight, but the sum of what they have done
 * in the last few seconds, each part fading out on its own.
 *
 * The distinction is the whole design. A running total is the MMO answer, and it works there because
 * the table is on screen and the roles are fixed. In a first person shooter nobody can see the
 * number, so an enemy that turns because of accumulated statistics reads as a bug. An enemy that
 * turns immediately after a shotgun goes off next to it reads as cause and effect. So threat here is
 * made of impulses that decay in a few seconds, and every impulse is expected to have a source the
 * other players can see and hear.
 *
 * SERVER SIDE. The AI runs on the authority and reads this there; a client's copy is never consulted
 * and is not replicated. Every action that raises threat already reaches the server on its own (a
 * client's shot goes through Server_ReportDamage, and so on), so raise it where the action lands.
 *
 * Nothing calls AddThreat from C++ yet except the damage hook below, which is off by default. That
 * is deliberate: which actions provoke, and how much, is a design question, and the abilities that
 * will mostly answer it do not exist yet. The function is BlueprintCallable so it can be wired up
 * without waiting for more C++.
 */
UCLASS(ClassGroup = (Coop), meta = (BlueprintSpawnableComponent))
class POLARITY_API UThreatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UThreatComponent();

	/** Make some noise. Amount is in the same units as distance in centimetres, because that is what
	 *  it is weighed against: a threat of 1.0 makes this player look about half as far away as they
	 *  are. DecaySeconds is how long it takes to fade to nothing, linearly.
	 *
	 *  Impulses stack. Firing three times in a second leaves three of them fading in parallel, which
	 *  is the intended behaviour: sustained noise holds attention, a single bang does not. */
	UFUNCTION(BlueprintCallable, Category = "Coop|Threat")
	void AddThreat(float Amount, float DecaySeconds = 4.0f);

	/** Everything still fading, summed. Zero when the player has been quiet. */
	UFUNCTION(BlueprintPure, Category = "Coop|Threat")
	float GetThreat() const;

	/** Wipe it. For a downed or respawning player, who should stop attracting anybody. */
	UFUNCTION(BlueprintCallable, Category = "Coop|Threat")
	void ClearThreat();

	/** Threat added per point of damage this player deals to an enemy.
	 *
	 *  Set to a TEST value, not a balanced one: at 0.01 a fifty-point melee hit is worth half a point
	 *  of threat, which makes that player look about a third closer for the next few seconds, and
	 *  sustained fire stacks. Enough to see the mechanic work and far too crude to ship. How hard
	 *  hurting something should make it turn on you is a balance decision. One number. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coop|Threat", meta = (ClampMin = "0.0"))
	float ThreatPerDamagePoint = 0.01f;

	/** How long a damage-driven impulse takes to fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coop|Threat", meta = (ClampMin = "0.1"))
	float DamageThreatDecaySeconds = 4.0f;

protected:
	/** One fading contribution. Held as a start time and a duration rather than a countdown so the
	 *  component needs no tick at all: the value is a function of the clock. */
	struct FThreatImpulse
	{
		float Amount = 0.0f;
		float StartTime = 0.0f;
		float Duration = 1.0f;
	};

	TArray<FThreatImpulse> Impulses;
};
