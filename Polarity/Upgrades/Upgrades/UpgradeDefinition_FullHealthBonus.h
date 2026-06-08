// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_FullHealthBonus.generated.h"

class UCurveFloat;

/**
 * Per-level tuning for the full-HP archetype upgrade.
 * The buff ramps from 0 (at HealthThreshold) to full strength (at 100% HP) along ScalingCurve.
 */
USTRUCT(BlueprintType)
struct FFullHealthBonusLevelData
{
	GENERATED_BODY()

	/** HP fraction (0..1) at/above which the buff starts ramping in. Default 0.85 = 85%. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Full Health Bonus", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HealthThreshold = 0.85f;

	/** Move-speed multiplier at full strength (1.25 = +25% at 100% HP). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Full Health Bonus", meta = (ClampMin = "1.0"))
	float MaxSpeedMultiplier = 1.25f;

	/** Melee-damage multiplier at full strength (1.25 = +25% at 100% HP). Applies to fist + melee weapon base damage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Full Health Bonus", meta = (ClampMin = "1.0"))
	float MaxMeleeDamageMultiplier = 1.25f;
};

/**
 * Data asset for the full-HP archetype: the closer the player is to full HP (above
 * HealthThreshold), the faster they move and the harder they hit in melee. Buff strength
 * scales along ScalingCurve (normalized [0..1] input → [0..1] output).
 *
 * Mutually exclusive with the low-HP archetype — set each definition's MutuallyExclusiveWith
 * to the other's UpgradeTag in the editor.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_FullHealthBonus : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	/** Per-level tuning. Length defines MaxLevel (auto-synced). Index 0 = Lv 1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Full Health Bonus")
	TArray<FFullHealthBonusLevelData> LevelData;

	/**
	 * Shared scaling curve: input = normalized depth into the HP zone [0..1] (0 at
	 * HealthThreshold, 1 at full HP), output = buff strength [0..1]. Null → linear
	 * (strength == input). Can reference the same asset as the low-HP upgrade.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Full Health Bonus")
	TObjectPtr<UCurveFloat> ScalingCurve;

	/** Returns data for the given level (1-indexed). Falls back to last/empty entry if out of range. */
	UFUNCTION(BlueprintPure, Category = "Full Health Bonus")
	const FFullHealthBonusLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
