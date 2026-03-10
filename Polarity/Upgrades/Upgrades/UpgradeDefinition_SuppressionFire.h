// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_SuppressionFire.generated.h"

/**
 * Data asset for the "Suppression Fire" upgrade.
 * Hitscan hits on ranged enemies (ShooterNPC) suppress their accuracy,
 * forcing a donut-pattern miss around the player. Duration scales with player speed.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_SuppressionFire : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	// ==================== Duration Tuning ====================

	/** Suppression duration at minimum speed threshold (seconds) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float MinSuppressionDuration = 0.5f;

	/** Suppression duration at max speed (seconds) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float MaxSuppressionDuration = 3.0f;

	// ==================== Speed Tuning ====================

	/** Minimum player speed to trigger suppression (cm/s). Below this, no effect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire", meta = (ClampMin = "0.0"))
	float MinSpeedThreshold = 100.0f;

	/** Player speed for full suppression duration (cm/s). Scales linearly from MinSpeed to this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire", meta = (ClampMin = "100.0"))
	float MaxSpeedForFullEffect = 1200.0f;

	// ==================== Stacking ====================

	/** Diminishing returns factor for stacking. Higher = faster diminishing.
	 *  Each stack adds: Duration / (1 + StackCount * Factor) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float DiminishingReturnsFactor = 0.5f;
};
