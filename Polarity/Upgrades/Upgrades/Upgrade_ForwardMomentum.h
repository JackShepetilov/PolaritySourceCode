// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_ForwardMomentum.generated.h"

class UUpgradeDefinition_ForwardMomentum;

/**
 * "Forward Momentum" Upgrade
 *
 * Rewards aggressive play by modifying hitscan damage based on the player's
 * movement direction relative to the target:
 *
 * - Moving toward target: up to +25% bonus damage (configurable)
 * - Standing still:       base damage (no modifier)
 * - Moving away:          up to -50% penalty (configurable)
 *
 * The effect scales with both approach angle (dot product) and movement speed.
 * Standing still or moving perpendicular to the target gives no modifier.
 *
 * All tuning parameters are configured via UUpgradeDefinition_ForwardMomentum.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Forward Momentum"))
class POLARITY_API UUpgrade_ForwardMomentum : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_ForwardMomentum();

	// ==================== State Queries ====================

	/** Get the current momentum multiplier (for UI display). Updated each time a shot is evaluated. */
	UFUNCTION(BlueprintPure, Category = "Forward Momentum")
	float GetLastMomentumMultiplier() const { return LastMomentumMultiplier; }

protected:

	virtual void OnUpgradeActivated() override;
	virtual float GetDamageMultiplier(AActor* Target) const override;

private:

	/** Cached pointer to our typed definition */
	TWeakObjectPtr<UUpgradeDefinition_ForwardMomentum> DefMomentum;

	/** Last computed multiplier (for UI readout) */
	mutable float LastMomentumMultiplier = 1.0f;
};
