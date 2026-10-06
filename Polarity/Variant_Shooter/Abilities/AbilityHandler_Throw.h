// AbilityHandler_Throw.h
// Shared pipeline for thrown abilities. Subclasses override SpawnThrown() and nothing else.
//
// Pipeline:
//   press   -> Start (wind-up)
//   Start ends, button still down -> AimLoop, until the button comes up
//   Start ends, button already up -> Throw                      (this is the tap)
//   Throw: payload leaves at ThrowReleaseTime, ability completes when the montage is over
//
// Everything is driven by timers rather than montage end delegates. A weapon swap or a mesh re-init
// replaces the FP AnimInstance and orphans a delegate, which is how a cast gets stuck; a timer does
// not care what happened to the animation.

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler.h"
#include "Engine/TimerHandle.h"
#include "AbilityHandler_Throw.generated.h"

class UAbilityDefinition_Throw;
class UAnimMontage;

UENUM(BlueprintType)
enum class EThrowPhase : uint8
{
	Idle,
	Start,
	AimLoop,
	Throw
};

UCLASS(Blueprintable, Abstract)
class POLARITY_API UAbilityHandler_Throw : public UAbilityHandler
{
	GENERATED_BODY()

public:

	virtual void OnActivate_Implementation() override;
	virtual void OnButtonReleased_Implementation() override;
	virtual void OnCancelRequested_Implementation() override;

	/** Draw the arc the throw would take right now, for one frame. Local and cosmetic: called by the
	 *  owning player's own character while the button is held, never by the authority's handler, so
	 *  there is no round trip between the mouse and the line.
	 *
	 *  Debug lines for now. The line is a placeholder for a real effect, not the effect. */
	static void DrawAimPreview(const class AShooterCharacter* Caster, const UAbilityDefinition_Throw* Def);

protected:

	/** The payload leaves the hand. Location and Rotation come from the definition's GetThrowStart,
	 *  the same answer the preview drew. Authority only, like every handler. */
	virtual void SpawnThrown(const FVector& Location, const FRotator& Rotation) {}

	UPROPERTY(BlueprintReadOnly, Category = "Throw|State")
	EThrowPhase Phase = EThrowPhase::Idle;

	UPROPERTY()
	TObjectPtr<UAbilityDefinition_Throw> CachedThrowDef;

	/** True from the press until the release reaches this machine. */
	bool bButtonHeld = false;

	/** Guards the release: one press throws one payload, whatever the timers do. */
	bool bThrown = false;

	FTimerHandle PhaseTimer;
	FTimerHandle ReleaseTimer;

	void EnterStart();
	void OnStartFinished();
	void EnterAimLoop();
	void EnterThrow();
	void ReleasePayload();
	void FinishThrow();
	void ResetToIdle();

	/** Schedule Callback for when a montage of this length at this rate is about to end. */
	void SchedulePhaseTimer(float SecondsLeft, void (UAbilityHandler_Throw::*Callback)());
};
