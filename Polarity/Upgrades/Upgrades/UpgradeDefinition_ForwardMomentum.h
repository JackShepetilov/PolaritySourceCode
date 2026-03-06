// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_ForwardMomentum.generated.h"

/**
 * Data asset for the "Forward Momentum" upgrade.
 * Rewards aggressive play: moving toward your target increases damage,
 * moving away decreases it. All parameters configurable in the editor.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_ForwardMomentum : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	// ==================== Tuning ====================

	/** Maximum damage bonus when running directly toward target at full speed (0.25 = +25%) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Forward Momentum", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ForwardBonusMultiplier = 0.25f;

	/** Maximum damage penalty when running directly away from target at full speed (0.5 = -50%) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Forward Momentum", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BackwardPenaltyMultiplier = 0.5f;

	/** Minimum movement speed to trigger any modifier (cm/s). Below this, no bonus or penalty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Forward Momentum", meta = (ClampMin = "0.0"))
	float MinSpeedThreshold = 100.0f;

	/** Speed at which the full bonus/penalty is applied (cm/s). Scales linearly from MinSpeed to this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Forward Momentum", meta = (ClampMin = "100.0"))
	float MaxSpeedForFullEffect = 1200.0f;
};
