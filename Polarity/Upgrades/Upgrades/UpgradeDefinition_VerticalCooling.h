// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_VerticalCooling.generated.h"

/**
 * Data asset for Vertical Cooling.
 *
 * While owned, vertical movement on the Z axis immediately restores HP, limited
 * by a periodically refreshed heal pool.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_VerticalCooling : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	/** HP restored per meter of absolute vertical movement. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vertical Cooling", meta = (ClampMin = "0.0"))
	float HealPerMeter = 1.0f;

	/** Heal budget available after each pool refresh. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vertical Cooling", meta = (ClampMin = "0.0"))
	float MaxHealPool = 30.0f;

	/** How often the heal pool resets back to MaxHealPool, in seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vertical Cooling", meta = (ClampMin = "0.1", Units = "s"))
	float PoolRefreshInterval = 10.0f;

	/** Per-frame vertical deltas smaller than this are ignored as movement jitter. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vertical Cooling", meta = (ClampMin = "0.0", Units = "cm"))
	float MinVerticalDeltaToCount = 2.0f;
};
