// AbilityHandler_Grapple.h

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler.h"
#include "Engine/TimerHandle.h"
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

protected:
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
