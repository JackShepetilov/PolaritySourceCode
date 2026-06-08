// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArenaAntenna.h"
#include "GateAntenna.generated.h"

class URunSubsystem;

/** Fired the moment the run-wide antenna count first reaches RequiredAntennaCount.
 *  Level BP can bind this to auto-open the path (no button press required). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGateUnlocked);

/** Fired when the player presses the gate's button BEFORE the threshold is met.
 *  Level BP binds this to pop the "need X more antennas" widget.
 *  CurrentCount = antennas activated so far, RequiredCount = threshold. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGateLockedAttempt, int32, CurrentCount, int32, RequiredCount);

/**
 * Special antenna that gates the path to the boss elevator.
 *
 * Unlike a normal AArenaAntenna (whose availability is driven by its ArenaManager's
 * combat lifecycle), this one watches the run-wide activated-antenna count in
 * URunSubsystem. Once RequiredAntennaCount antennas have been activated across the
 * run's sublevels, the gate flips to AvailablePostFight — the inherited beacon VFX
 * turns on, reusing the same visual language the player already learned, and the
 * button becomes pressable.
 *
 * Designer places it in the central hub, sets RequiredAntennaCount in Details, and
 * wires the events in Level BP:
 *   - OnGateUnlocked      → optional auto-open the door when N is reached.
 *   - OnActivated (base)  → open the door when the player presses the button.
 *   - OnGateLockedAttempt → show the "need X more antennas" widget on an early press.
 */
UCLASS(Blueprintable)
class POLARITY_API AGateAntenna : public AArenaAntenna
{
	GENERATED_BODY()

public:
	/** How many regular antennas must be activated this run before this gate unlocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate", meta = (ClampMin = "1"))
	int32 RequiredAntennaCount = 3;

	// ==================== Events ====================

	/** Threshold reached — beacon is now on. Bind here for auto-open behaviour. */
	UPROPERTY(BlueprintAssignable, Category = "Gate|Events")
	FOnGateUnlocked OnGateUnlocked;

	/** Player pressed the button while still locked. Bind here to show the gating widget. */
	UPROPERTY(BlueprintAssignable, Category = "Gate|Events")
	FOnGateLockedAttempt OnGateLockedAttempt;

	// ==================== State (read-only) ====================

	/** True once RequiredAntennaCount has been reached this run. */
	UFUNCTION(BlueprintPure, Category = "Gate")
	bool IsGateUnlocked() const { return bUnlocked; }

	// ==================== Overrides ====================

	/** Intercepts presses: while locked, broadcasts OnGateLockedAttempt instead of activating. */
	virtual void TryActivate() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Bound to URunSubsystem::OnAntennaCountChanged — re-evaluates unlock on every change. */
	UFUNCTION()
	void HandleAntennaCountChanged(int32 NewCount);

	/** Unlocks (beacon on + OnGateUnlocked) the first time Count >= RequiredAntennaCount. */
	void EvaluateUnlock(int32 Count);

	/** Convenience accessor for the run subsystem (may be null very early / in PIE edge cases). */
	URunSubsystem* GetRunSubsystem() const;

	/** Latched so we only unlock (and broadcast) once per run. */
	bool bUnlocked = false;
};
