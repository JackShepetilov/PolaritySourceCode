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
class UAudioComponent;

/** Internal phase of the reworked charged punch. */
enum class EChargedPunchPhase : uint8
{
	None,
	Rising,          // button held past MinHoldTime: the player rises higher and higher
	Buffer,          // released: hovering for ReleaseBufferTime, a dropkick may replace the slam
	Slamming,        // descending fast → AoE slam on landing
	DropkickFollow   // a dropkick replaced the slam: waiting for it to hit, then AoE on the target
};

/**
 * Runtime logic for the reworked Charged Punch upgrade. See UpgradeDefinition_ChargedPunch.h.
 *
 * Flow:
 *   Hold melee past MinHoldTime  -> Rising: drain the shared health-pickup pool and lift the
 *                                   player upward (up to MaxRiseHeight), gravity disabled.
 *   Release                      -> Buffer: hover for ReleaseBufferTime so the player can input a
 *                                   dropkick.
 *     - dropkick during buffer   -> DropkickFollow: the dropkick dives at its target (driven by
 *                                   MeleeAttackComponent); when it connects (OnDropKickHit) the
 *                                   slam's AoE sphere fires centered on that target.
 *     - no dropkick              -> Slamming: fast descent, then an AoE slam at the landing point.
 *
 * The dropkick is detected via MeleeAttackComponent::OnDropKickStarted / OnDropKickHit (bound only
 * during Buffer + DropkickFollow), so no change to AShooterCharacter::DoMeleeAttack is needed — the
 * buffer phase deliberately reports IsBusy() == false so the re-press reaches the melee component.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Charged Punch"))
class POLARITY_API UUpgrade_ChargedPunch : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_ChargedPunch();

	/** Is the player currently charging (rising)? */
	UFUNCTION(BlueprintPure, Category = "ChargedPunch")
	bool IsCharging() const { return Phase == EChargedPunchPhase::Rising; }

	/** True during a post-release motion phase (slam descent or dropkick follow). */
	UFUNCTION(BlueprintPure, Category = "ChargedPunch")
	bool IsLunging() const { return Phase == EChargedPunchPhase::Slamming || Phase == EChargedPunchPhase::DropkickFollow; }

	/**
	 * Should the regular swing be suppressed? True while rising / slamming / dropkick-following.
	 * Deliberately FALSE during the hover Buffer so the player's dropkick re-press reaches
	 * AShooterCharacter::DoMeleeAttack → MeleeAttackComponent. Named IsBusy() (not IsActive()) to
	 * avoid colliding with UActorComponent::IsActive().
	 */
	UFUNCTION(BlueprintPure, Category = "ChargedPunch")
	bool IsBusy() const;

	/** Hold elapsed seconds (0 if not pressed). Capped at the definition's MaxHoldTime. */
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

	/** Active charge-loop audio (spawned at rise start, stopped on release/cancel). */
	UPROPERTY()
	TObjectPtr<UAudioComponent> ActiveChargeLoop;

	EChargedPunchPhase Phase = EChargedPunchPhase::None;

	/** True while the melee button is held. */
	bool bButtonHeld = false;

	/** Seconds the button has been held this press (capped at MaxHoldTime). */
	float HoldElapsed = 0.0f;

	/** Fractional pickups drained this tick — once it exceeds 1.0 we consume an integer count. */
	float DrainAccumulator = 0.0f;

	/** Bonus damage captured at release so the slam keeps the charge level after HoldElapsed resets. */
	float ChargedBonusDamage = 0.0f;

	/** Hover timer for the Buffer phase. */
	float BufferElapsed = 0.0f;

	/** Timeout timer for the DropkickFollow phase. */
	float DropkickWaitElapsed = 0.0f;

	/** Z the player was at when the rise started (cap reference for MaxRiseHeight). */
	float RiseStartZ = 0.0f;

	/** Ground point found at slam start (the descent target). */
	FVector SlamGroundTarget = FVector::ZeroVector;

	/** Saved CharacterMovement state captured at rise start, restored when the sequence ends. */
	TEnumAsByte<EMovementMode> SavedMovementMode = MOVE_Walking;
	float SavedGravityScale = 1.0f;
	bool bMovementSaved = false;

	/** True while we own the MeleeMesh view (entered at rise start). */
	bool bOwnsMeleeMeshView = false;

	/** True while OnDropKickStarted / OnDropKickHit are bound (Buffer + DropkickFollow). */
	bool bDropkickDelegatesBound = false;

	// ==================== Input / event callbacks ====================

	UFUNCTION() void HandleHoldStarted();
	UFUNCTION() void HandleHoldReleased();
	UFUNCTION() void HandleDropKickStarted();
	UFUNCTION() void HandleDropKickHit(AActor* HitActor, const FVector& HitLocation, float Damage);

	// ==================== Phase logic ====================

	void EnterRisingPhase();
	void TickRise(float DeltaTime);
	void EnterBufferPhase();
	void TickBuffer(float DeltaTime);
	void EnterSlamPhase();
	void TickSlam(float DeltaTime);
	void TickDropkickFollow(float DeltaTime);

	/** Radial AoE: overlap sphere at Origin, damage + knockback + VFX/sound per target, feed Combo. */
	void DoSlamAoE(const FVector& Origin);

	/** End the whole sequence: stop charge SFX, unbind dropkick delegates, restore movement/mesh, reset to None. */
	void EndSequence();

	void StopChargeLoop();
	void CaptureMovement();
	void RestoreMovement();
	void BindDropkickDelegates();
	void UnbindDropkickDelegates();

	float EvaluateBonusDamage(float HoldTime) const;
	UUpgrade_Combo* FindComboUpgrade() const;
};
