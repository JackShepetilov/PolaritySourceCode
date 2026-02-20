// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_360Shot.generated.h"

class UNiagaraSystem;
class USoundBase;

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
 */
UCLASS(BlueprintType, meta = (DisplayName = "360 Shot"))
class POLARITY_API UUpgrade_360Shot : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_360Shot();

	// ==================== Tuning ====================

	/** Fixed bonus damage dealt by the 360 shot (on top of normal shot damage) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "360 Shot", meta = (ClampMin = "1.0"))
	float BonusDamage = 500.0f;

	/** Time window to complete the 360-degree rotation (seconds) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "360 Shot", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float SpinTimeWindow = 1.5f;

	/** Duration of the charged state after completing the spin (seconds) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "360 Shot", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float ChargedDuration = 1.0f;

	/** Minimum rotation speed to count toward spin (degrees/sec). Prevents slow creeping rotations. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "360 Shot", meta = (ClampMin = "0.0"))
	float MinRotationSpeed = 180.0f;

	// ==================== VFX/SFX ====================

	/** Special beam VFX for the 360 shot (overrides normal beam) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "360 Shot|Effects")
	TObjectPtr<UNiagaraSystem> ChargedBeamFX;

	/** Color of the charged beam */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "360 Shot|Effects")
	FLinearColor ChargedBeamColor = FLinearColor(1.0f, 0.3f, 0.05f, 1.0f);

	/** Sound played when charged state activates */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "360 Shot|Effects")
	TObjectPtr<USoundBase> ChargedReadySound;

	/** Sound played when the 360 shot fires */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "360 Shot|Effects")
	TObjectPtr<USoundBase> ChargedFireSound;

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
