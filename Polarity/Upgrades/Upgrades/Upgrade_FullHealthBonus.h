// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_FullHealthBonus.generated.h"

class UUpgradeDefinition_FullHealthBonus;

/**
 * Full-HP archetype upgrade.
 *
 * While the player is at/above HealthThreshold, they move faster and hit harder in melee.
 * Both buffs scale with how close the player is to full HP (normalized [0..1] along the
 * definition's ScalingCurve). Mutually exclusive with the low-HP archetype.
 *
 * - Speed: drives UApexMovementComponent::ExternalSpeedMultiplier (event-driven off OnDamaged,
 *   which fires on damage / heal / regen — no tick needed).
 * - Melee: overrides GetMeleeDamageMultiplier (stateless, queried per hit → applies to fist + sword).
 */
UCLASS(BlueprintType, meta = (DisplayName = "Full Health Bonus"))
class POLARITY_API UUpgrade_FullHealthBonus : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_FullHealthBonus();

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual float GetMeleeDamageMultiplier(AActor* Target) const override;

private:

	/** Bound to AShooterCharacter::OnDamaged (fires on damage / heal / regen). */
	UFUNCTION()
	void HandleHealthChanged(float LifePercent, float ArmorPercent);

	/** Buff strength [0..1] from the current HP fraction (0 below threshold, ramps to 1 at full HP). */
	float ComputeStrength() const;

	/** Write the current speed multiplier onto the movement component (1.0 when below threshold). */
	void RefreshSpeedMultiplier();

	/** Cached typed definition. */
	TWeakObjectPtr<UUpgradeDefinition_FullHealthBonus> CachedDef;
};
