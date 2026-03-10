// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_SuppressionFire.generated.h"

class UUpgradeDefinition_SuppressionFire;

/**
 * "Suppression Fire" Upgrade
 *
 * When the player hits a ranged enemy (ShooterNPC or subclass) with a hitscan weapon,
 * the enemy's accuracy is suppressed into a donut pattern — shots fly close to the player
 * but are guaranteed to miss.
 *
 * Duration scales with the player's movement speed at the moment of the hit.
 * Standing still or moving too slowly produces no effect.
 * Repeated hits stack duration with diminishing returns.
 *
 * The enemy's AIAccuracyComponent broadcasts OnSuppressionStart/End for Blueprint VFX.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Suppression Fire"))
class POLARITY_API UUpgrade_SuppressionFire : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_SuppressionFire();

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnOwnerDealtDamage(AActor* Target, float Damage, bool bKilled) override;

private:

	/** Cached pointer to our typed definition */
	TWeakObjectPtr<UUpgradeDefinition_SuppressionFire> DefSuppression;
};
