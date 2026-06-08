// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_LowHealthDefense.generated.h"

class UCurveFloat;

/**
 * Per-level tuning for the low-HP archetype upgrade.
 * Effect strength ramps from 0 (at HealthThreshold) to full (at 0 HP) along ScalingCurve.
 */
USTRUCT(BlueprintType)
struct FLowHealthDefenseLevelData
{
	GENERATED_BODY()

	/** HP fraction (0..1) below which the upgrade activates. Default 0.35 = 35% (match the player's low-HP threshold). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Low Health Defense", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HealthThreshold = 0.35f;

	/** Radius (cm) around the player within which enemies are slowed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Low Health Defense", meta = (ClampMin = "0.0"))
	float SlowRadius = 1200.0f;

	/** CustomTimeDilation applied to nearby enemies at full strength (0.4 = 40% speed = strong slow-mo). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Low Health Defense", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MinEnemyTimeDilation = 0.4f;

	/** Enemy-bolt speed multiplier at full strength (0.2 = bolts at 20% speed = very dodgeable at 0 HP).
	 *  At/above the threshold it's 1.0 (full default speed); ramps to this by 0 HP along ScalingCurve. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Low Health Defense", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MinBoltSpeedMultiplier = 0.2f;
};

/**
 * Data asset for the low-HP archetype. While the player is below HealthThreshold:
 *  - nearby enemies are slowed (CustomTimeDilation), strength scaling toward 0 HP, and
 *  - enemy hitscans against the player become dodgeable travelling bolts (via
 *    AShooterCharacter::SetLowHealthDefenseActive, handled weapon-side in PerformSimpleHitscan).
 * Strength scales along ScalingCurve (normalized [0..1] input → [0..1] output).
 *
 * Mutually exclusive with the full-HP archetype — set each definition's MutuallyExclusiveWith
 * to the other's UpgradeTag in the editor.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_LowHealthDefense : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	/** Per-level tuning. Length defines MaxLevel (auto-synced). Index 0 = Lv 1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Low Health Defense")
	TArray<FLowHealthDefenseLevelData> LevelData;

	/**
	 * Shared scaling curve: input = normalized depth into the low-HP zone [0..1] (0 at
	 * HealthThreshold, 1 at 0 HP), output = effect strength [0..1]. Null → linear
	 * (strength == input). Can reference the same asset as the full-HP upgrade.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Low Health Defense")
	TObjectPtr<UCurveFloat> ScalingCurve;

	/** Returns data for the given level (1-indexed). Falls back to last/empty entry if out of range. */
	UFUNCTION(BlueprintPure, Category = "Low Health Defense")
	const FLowHealthDefenseLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
