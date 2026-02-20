// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_360Shot.generated.h"

class UUpgradeDefinition_360Shot;

/**
 * "360 Shot" Upgrade
 *
 * Tracks player yaw rotation. When the player completes a full 360-degree
 * spin within a time window, activates a "charged" state for a brief duration.
 * The next rifle (hitscan) shot during the charged state deals massive fixed
 * damage and spawns a special beam VFX instead of the normal one.
 *
 * The bonus damage is applied ON TOP of the normal shot — the regular shot
 * fires normally, and the upgrade adds a separate high-damage hit.
 *
 * All tuning parameters and asset references are configured via
 * UUpgradeDefinition_360Shot (DataAsset) in the editor.
 */
UCLASS(BlueprintType, meta = (DisplayName = "360 Shot"))
class POLARITY_API UUpgrade_360Shot : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_360Shot();

	// ==================== State Queries ====================

	/** Is the 360 shot currently charged and ready to fire? */
	UFUNCTION(BlueprintPure, Category = "360 Shot")
	bool IsCharged() const { return bIsCharged; }

	/** Current accumulated rotation (0-360 degrees) */
	UFUNCTION(BlueprintPure, Category = "360 Shot")
	float GetAccumulatedRotation() const { return AccumulatedYaw; }

protected:

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual void OnWeaponFired() override;

private:

	/** Cached pointer to our typed definition (avoids casting every frame) */
	TWeakObjectPtr<UUpgradeDefinition_360Shot> Def360;

	/** Accumulated absolute yaw rotation within the time window */
	float AccumulatedYaw = 0.0f;

	/** Previous frame's yaw for delta calculation */
	float PreviousYaw = 0.0f;

	/** Is the first frame (no valid PreviousYaw yet)? */
	bool bFirstFrame = true;

	/** Is the charged state active? */
	bool bIsCharged = false;

	/** Timer for resetting accumulated rotation if spin is too slow */
	float TimeSinceLastSignificantRotation = 0.0f;

	/** Timer handle for charged state expiration */
	FTimerHandle ChargedExpirationTimer;

	/** Activate the charged state */
	void ActivateCharged();

	/** Deactivate the charged state (timer callback or after shot) */
	void DeactivateCharged();

	/** Execute the bonus 360 shot (extra damage + VFX) */
	void Execute360Shot();

	/** Spawn the special charged beam VFX */
	void SpawnChargedBeamEffect(const FVector& Start, const FVector& End);
};
