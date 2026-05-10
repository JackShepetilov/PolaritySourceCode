// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_AirDash.generated.h"

/**
 * Per-level tuning for the Air Dash upgrade.
 * Author one entry per level in DataAsset (index 0 = level 1, etc).
 * Length of LevelData defines MaxLevel — auto-synced via PostEditChangeProperty.
 */
USTRUCT(BlueprintType)
struct FAirDashLevelData
{
	GENERATED_BODY()

	/** How many air dashes can the player chain before landing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Dash", meta = (ClampMin = "1"))
	int32 MaxCharges = 1;

	/** Cooldown between consecutive dashes (seconds). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Dash", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 0.5f;

	/** Impulse strength multiplier applied per dash (1.0 = base, 1.5 = stronger, etc). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Dash", meta = (ClampMin = "0.0"))
	float ImpulseMultiplier = 1.0f;
};

/**
 * Data asset for the "Air Dash" unlock upgrade.
 * Designer authors LevelData (one entry per level) — Upgrade_AirDash reads
 * the entry matching CurrentLevel on OnUpgradeActivated / OnLevelChanged.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_AirDash : public UUpgradeDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Per-level tuning. Length defines MaxLevel (auto-synced).
	 * Designer adds entries in the editor: index 0 = Lv 1, index 1 = Lv 2, etc.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Dash")
	TArray<FAirDashLevelData> LevelData;

	/** Returns data for the given level (1-indexed). Falls back to last entry if out of range. */
	UFUNCTION(BlueprintPure, Category = "Air Dash")
	const FAirDashLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
