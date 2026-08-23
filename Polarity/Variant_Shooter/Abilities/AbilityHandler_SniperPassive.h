// AbilityHandler_SniperPassive.h
// Counts the ground the Sniper covers between shots and turns it into damage that ignores shields.

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler.h"
#include "AbilityHandler_SniperPassive.generated.h"

/**
 * One accumulator, one latch and one list.
 *
 * The ACCUMULATOR adds the distance the owner actually moved each frame. It is filled from the tick
 * rather than from the movement component on purpose: every way of moving counts the same, and a
 * grapple swing, a fall and a walk are all just ground covered.
 *
 * The LATCH is what the current shot is worth. It exists because the two network routes into a shot
 * disagree about order: on the host, Fire() computes the damage and only then says a shot happened,
 * while a client's shot reaches the server as two RPCs with the fire report FIRST. Latching at the
 * start of Fire, on both routes, is the only moment they agree on -- without it the server would
 * either spend the accumulator before reading it or never spend it at all, and a client's Sniper
 * would deal maximum damage forever.
 *
 * The LIST is the enemies this Sniper has hit, kept so the overhead readout appears over targets the
 * player has engaged rather than over everything in the room.
 *
 * WHICH MACHINE. The damage is applied by the server, from the server's own copy of these numbers:
 * it watches the same pawn move and hears about the same shots, so it arrives at the same answer
 * without a single byte on the wire. The client's copy exists only to drive its own readout, which
 * is a prediction of the next shot and never the source of a damage number.
 */
UCLASS()
class POLARITY_API UAbilityHandler_SniperPassive : public UAbilityHandler
{
	GENERATED_BODY()

public:
	virtual void OnEquip_Implementation() override;
	virtual void OnUnequip_Implementation() override;

	virtual void OnPassiveTick(float DeltaTime) override;
	virtual void OnOwnerFiredWeapon() override;
	virtual void OnOwnerDealtDamage(AActor* Target, float Damage, bool bKilled) override;

	virtual float GetBonusPierceDamage(const AActor* Target) const override;
	virtual float GetPredictedPierceDamage(const AActor* Target) const override;
	virtual bool GetPredictedShotDamage(const AActor* Target, float& OutDamage) const override;

	/** Ground covered since the last shot, already clamped to what the passive will pay for. */
	UFUNCTION(BlueprintPure, Category = "Sniper Passive")
	float GetTravelSinceLastShot() const { return TravelSinceLastShot; }

	/** 0..1 fill of the bonus, for a bar on the player's own HUD. */
	UFUNCTION(BlueprintPure, Category = "Sniper Passive")
	float GetTravelFraction() const;

	/** What the NEXT shot's own damage would be. */
	UFUNCTION(BlueprintPure, Category = "Sniper Passive")
	float GetPendingPierceDamage() const;

protected:
	/** Turn a travelled distance into this passive's damage. One place, so the number the readout
	 *  shows and the number the server applies cannot drift apart. */
	float PierceDamageForTravel(float Travel) const;

	/** Drop entries whose target died or whose memory expired, so the list cannot grow for the
	 *  length of a run. */
	void PruneEngagedTargets();

	/** True when this machine has any business counting: the owning client, for its own readout, and
	 *  the authority, which is the one that actually applies the damage. */
	bool ShouldAccumulateHere() const;

	/** Ground covered since the last shot, clamped to DistanceForFullBonus. */
	float TravelSinceLastShot = 0.0f;

	/** What the shot currently being resolved is worth, latched when the trigger was pulled. */
	float TravelAtLastShot = 0.0f;

	/** Where the owner was last frame. Invalid until the first tick, which is why it has a flag
	 *  rather than a zero-vector sentinel -- the origin is a legal place to stand. */
	FVector LastOwnerLocation = FVector::ZeroVector;
	bool bHasLastLocation = false;

	/** Enemies this Sniper has hit, and the world time of the last hit on each. */
	TMap<TWeakObjectPtr<AActor>, float> EngagedTargets;
};
