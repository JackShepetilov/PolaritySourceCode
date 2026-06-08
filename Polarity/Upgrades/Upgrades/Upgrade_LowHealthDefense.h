// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_LowHealthDefense.generated.h"

class UUpgradeDefinition_LowHealthDefense;
class AShooterNPC;

/**
 * Low-HP archetype upgrade.
 *
 * While the player is below HealthThreshold:
 *  - Sets AShooterCharacter::bLowHealthDefenseActive so enemy weapons convert their hitscan into
 *    dodgeable travelling bolts (UEnemyBeamBoltSubsystem).
 *  - Slows nearby enemies via CustomTimeDilation, scaled by how deep the player is into the zone
 *    (normalized [0..1] along the definition's ScalingCurve).
 *
 * Mutually exclusive with the full-HP archetype. Ticks always (cheap HP read); the heavier
 * overlap scan is throttled and runs only while active.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Low Health Defense"))
class POLARITY_API UUpgrade_LowHealthDefense : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_LowHealthDefense();

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	/** Normalized effect strength [0..1] from current HP (0 at/above threshold, ramps to 1 at 0 HP). */
	float ComputeStrength() const;

	/** Apply CustomTimeDilation to enemies within SlowRadius; restore those that left. */
	void RefreshSlowedEnemies(float Strength);

	/** Restore CustomTimeDilation = 1.0 on every enemy we slowed, then clear the tracking set. */
	void RestoreAllSlowedEnemies();

	/** Cached typed definition. */
	TWeakObjectPtr<UUpgradeDefinition_LowHealthDefense> CachedDef;

	/** Enemies currently slowed by us (so we can restore them when they leave range / effect ends). */
	TSet<TWeakObjectPtr<AShooterNPC>> SlowedEnemies;

	/** Whether the upgrade is currently in its active (below-threshold) state. */
	bool bActiveState = false;

	/** Throttle accumulator for the slow-enemies overlap scan. */
	float ScanAccumulator = 0.0f;
};
