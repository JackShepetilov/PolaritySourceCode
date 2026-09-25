// AbilityHandler_Grapple.h

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler.h"
#include "Engine/TimerHandle.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "AbilityHandler_Grapple.generated.h"

/**
 * Throws the line and decides when it lets go. It does NOT move anybody.
 *
 * The swing lives in UApexMovementComponent, inside the simulated move, because everything that
 * writes Velocity has to be there or the server never re-runs it -- the symptom of getting that
 * wrong is always the same, a mechanic that works perfectly for the host and stutters for everybody
 * else. This handler picks the anchor, waits out the hook's flight, and hands the movement component
 * an intent through the character, which sets it on the authority and on the machine that predicts
 * the character both.
 *
 * Authority only, like every handler: it decides something about the world.
 */
UCLASS()
class POLARITY_API UAbilityHandler_Grapple : public UAbilityHandler
{
	GENERATED_BODY()

public:
	virtual void OnActivate_Implementation() override;
	virtual void OnButtonReleased_Implementation() override;
	virtual void OnCancelRequested_Implementation() override;
	virtual void OnUnequip_Implementation() override;

	/** Watches for the swing ending on its own. Arrival at the anchor and the duration running out
	 *  are both decided inside the movement simulation, on both machines independently, so the
	 *  ability has to NOTICE that it ended rather than be the one to end it. */
	virtual void OnActiveTick(float DeltaTime) override;

	/** The dropped weapon this character is looking at from inside the fetch radius, or null.
	 *
	 *  Static and public because the brackets have to run the SAME rule on the owning client that
	 *  the claim is checked against here, or the brackets promise a fetch the throw then refuses.
	 *  Cheap enough for a frame: one pass over the drops in the world, one trace for the winner. */
	static class ADroppedRangedWeapon* FindFetchTarget(const class AShooterCharacter* Caster,
		const class UAbilityDefinition_Grapple* Def);

	/** Whether this drop can be fetched by this character right now, ignoring where they look.
	 *  ExtraRadius is the round-trip slack the server grants a client's claim. */
	static bool IsFetchable(const class AShooterCharacter* Caster, const class ADroppedRangedWeapon* Drop,
		const class UAbilityDefinition_Grapple* Def, float ExtraRadius = 0.0f);

protected:
	/** Throw at a dropped weapon instead of the world. The hook flies, bites the drop, and the drop
	 *  then flies back to the caster on its own pull. Never swings, never costs the cooldown. */
	void StartFetch(class ADroppedRangedWeapon* Drop);

	/** The fetch hook has reached the drop: start its pull toward the caster and end the ability. */
	void FinishFetch();

	/** Called for any cancel while a fetch hook is still in the air. */
	void AbortFetch();

	/** The drop the hook is flying at. Weak: another player may take it first. */
	TWeakObjectPtr<class ADroppedRangedWeapon> PendingFetch;

	/** True between a fetch throw and the hook reaching the drop. Separate from PendingFetch because
	 *  the drop can be destroyed mid-flight and the ability still has to be ended. */
	bool bFetchInFlight = false;

	FTimerHandle FetchTravelTimer;

	/** The hook has arrived: attach the line and start pulling. */
	void AttachLine();

	/** Let go, for any reason, and finish the ability. Idempotent. */
	void ReleaseLine(bool bCancelled);

	/** Where the hook is flying to, chosen by the trace in OnActivate. */
	FVector PendingAnchor = FVector::ZeroVector;

	/** True between the throw and the release, so a second press or a double release does nothing. */
	bool bLineOut = false;

	/** True once the line has actually bitten, so the tick knows the difference between "still in
	 *  flight" and "the swing ended". */
	bool bLineAttached = false;

	FTimerHandle HookTravelTimer;
};
