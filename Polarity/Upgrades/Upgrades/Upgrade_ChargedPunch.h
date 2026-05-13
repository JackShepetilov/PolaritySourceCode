// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Engine/EngineTypes.h"
#include "Upgrade_ChargedPunch.generated.h"

class UUpgradeDefinition_ChargedPunch;
class UUpgradeManagerComponent;
class UMeleeAttackComponent;
class UUpgrade_Combo;
class UNiagaraComponent;
class UAudioComponent;

/**
 * Runtime logic for the Charged Punch upgrade. See UpgradeDefinition_ChargedPunch.h
 * for design.
 *
 * Flow:
 *   AShooterCharacter::OnMeleeChargeHoldStarted -> HandleHoldStarted
 *     starts a hold timer
 *   TickComponent
 *     once HoldElapsed > MinHoldTime: enter ChargingPhase, drain pool, spawn VFX
 *     stops draining if pool reaches 0
 *   AShooterCharacter::OnMeleeChargeHoldReleased -> HandleHoldReleased
 *     if charging: do capsule sweep, deal damage, lunge player, feed Combo
 *     else: no-op (regular jab from Triggered already played)
 */
UCLASS(BlueprintType, meta = (DisplayName = "Charged Punch"))
class POLARITY_API UUpgrade_ChargedPunch : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_ChargedPunch();

	/** Is the player currently charging? */
	UFUNCTION(BlueprintPure, Category = "ChargedPunch")
	bool IsCharging() const { return bIsCharging; }

	/** True while flight phase is interpolating the player toward the endpoint. */
	UFUNCTION(BlueprintPure, Category = "ChargedPunch")
	bool IsLunging() const { return bIsLunging; }

	/**
	 * Aggregate "is the upgrade in any active phase that should suppress regular
	 * melee?" — covers charging, lunging, AND post-lunge anim wait (the air
	 * montage is still playing). Used by AShooterCharacter::DoMeleeAttack to
	 * filter out repeated Triggered pulses while the charged punch owns the view.
	 */
	UFUNCTION(BlueprintPure, Category = "ChargedPunch")
	bool IsActive() const;

	/** Hold elapsed seconds (0 if not pressed). Capped at definition's MaxHoldTime. */
	UFUNCTION(BlueprintPure, Category = "ChargedPunch")
	float GetHoldElapsed() const { return HoldElapsed; }

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	TWeakObjectPtr<UUpgradeDefinition_ChargedPunch> CachedDef;
	TWeakObjectPtr<UUpgradeManagerComponent> CachedUpgradeManager;
	TWeakObjectPtr<UMeleeAttackComponent> CachedMeleeComp;

	/** Active endpoint-preview VFX instance (spawned at hold-start, destroyed on release/cancel). */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveEndpointVFX;

	/** Active charge-loop audio (spawned at threshold, stopped on release/cancel). */
	UPROPERTY()
	TObjectPtr<UAudioComponent> ActiveChargeLoop;

	/** True while button is held. */
	bool bButtonHeld = false;

	/** True once HoldElapsed crossed MinHoldTime AND pool was non-empty at that moment. */
	bool bIsCharging = false;

	/** Seconds the button has been held this press. */
	float HoldElapsed = 0.0f;

	/** Fractional pickups drained this tick — once it exceeds 1.0 we consume an integer count. */
	float DrainAccumulator = 0.0f;

	// ==================== Lunge Flight State ====================
	// Active between StartLunge() and FinishLunge(). Tick interpolates LungeStart -> LungeEnd
	// over LungeTotalDuration seconds via SetActorLocation (gravity disabled, mesh swapped).

	/** True while the player is mid-flight after a charged-punch release. */
	bool bIsLunging = false;

	FVector LungeStart = FVector::ZeroVector;
	FVector LungeEnd = FVector::ZeroVector;
	float LungeElapsed = 0.0f;
	float LungeTotalDuration = 0.15f;

	/** Saved CharacterMovement state restored when lunge finishes. */
	TEnumAsByte<EMovementMode> SavedMovementMode = MOVE_Walking;
	float SavedGravityScale = 1.0f;

	// ==================== Input Callbacks ====================

	UFUNCTION()
	void HandleHoldStarted();

	UFUNCTION()
	void HandleHoldReleased();

	// ==================== Charge Lifecycle ====================

	/** First time HoldElapsed crosses MinHoldTime — spawn VFX/audio, mark bIsCharging. */
	void EnterChargingState();

	/** Stop charging (cancel or release) — destroy VFX/audio, clear state. */
	void ExitChargingState();

	/** Execute the punch (capsule sweep + lunge + damage). Called on release if bIsCharging. */
	void ExecutePunch();

	/**
	 * Compute the endpoint of the punch given current hold elapsed.
	 * Forward-trace from camera; clamp at first geometry hit; otherwise use desired distance.
	 * Returns (endpoint, distance).
	 */
	void ComputeEndpoint(FVector& OutEndpoint, float& OutDistance) const;

	/** Sample distance from curve (or fallback) for a given hold time. */
	float EvaluateDistance(float HoldTime) const;

	/** Sample bonus damage from curve (or fallback) for a given hold time. */
	float EvaluateBonusDamage(float HoldTime) const;

	/** Find Combo upgrade on the same character, if active, for multi-kill feed-back. */
	UUpgrade_Combo* FindComboUpgrade() const;

	// ==================== Lunge Lifecycle ====================

	/** Start the visual lunge phase: lift player, disable gravity, switch mesh, play montage. */
	void StartLunge(const FVector& StartPos, const FVector& EndPos);

	/** Tick interpolation while bIsLunging — moves player from LungeStart toward LungeEnd. */
	void TickLunge(float DeltaTime);

	/** Restore everything that StartLunge changed (mesh view, gravity, movement mode). */
	void FinishLunge();
};
