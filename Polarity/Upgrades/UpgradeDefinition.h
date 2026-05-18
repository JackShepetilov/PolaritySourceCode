// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "SkillTypes.h"
#include "UpgradeDefinition.generated.h"

class UUpgradeComponent;

/**
 * One "stat row" displayed on the upgrade card in the level-up Choice UI — Hades-style.
 * E.g. { Label = "Doom Damage", Value = "100" }.
 */
USTRUCT(BlueprintType)
struct FUpgradeStat
{
	GENERATED_BODY()

	/** Stat name shown on the card. E.g. "Doom Damage", "Cooldown", "Bonus per Stack". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade Stat")
	FText Label;

	/** Pre-formatted value shown on the card. E.g. "100", "0.5s", "+65%", "x1.5". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade Stat")
	FText Value;
};

/**
 * UI data shown on the upgrade card for ONE level. Designer authors one entry per level
 * in UUpgradeDefinition::LevelDisplays (index 0 = Lv 1, index 1 = Lv 2, ...).
 *
 * Description here overrides the base Description for that specific level.
 * Stats is the Hades-style "label: value" list shown under the description.
 */
USTRUCT(BlueprintType)
struct FUpgradeLevelDisplay
{
	GENERATED_BODY()

	/** Description shown on the card when this level is being offered. Leave empty to fall back to UUpgradeDefinition::Description. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade Level Display", meta = (MultiLine = "true"))
	FText Description;

	/** Stat rows shown on the card when this level is being offered. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade Level Display")
	TArray<FUpgradeStat> Stats;
};

/**
 * Data asset that defines an upgrade's metadata and logic class.
 * Created in the editor Content Browser. One per upgrade.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	/** Unique gameplay tag identifier for this upgrade (used for save/load and dedup) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade")
	FGameplayTag UpgradeTag;

	/** Skill that this upgrade belongs to. Choice panel pulls from the pool of the levelled-up skill. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade")
	ESkillCategory Category = ESkillCategory::Movement;

	/** Maximum level this upgrade can reach. 1 = single-level (legacy behaviour, drops out of pool after first grant). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade", meta = (ClampMin = "1"))
	int32 MaxLevel = 1;

	/** Display name shown in UI and on world pickup */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade|UI")
	FText DisplayName;

	/** Generic fallback description — used only if the matching LevelDisplays entry has an empty Description. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade|UI", meta = (MultiLine = "true"))
	FText Description;

	/** Icon for UI and world pickup hologram */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade|UI")
	TObjectPtr<UTexture2D> Icon;

	/** The component class that implements this upgrade's runtime logic */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade")
	TSubclassOf<UUpgradeComponent> ComponentClass;

	/** Visual tier for pickup VFX treatment (color, particles, etc.) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade|UI", meta = (ClampMin = "1", ClampMax = "5"))
	int32 Tier = 1;

	/**
	 * Per-level UI data: one entry per level (index 0 = Lv 1, ...). Designer authors these
	 * in the DataAsset — Description and Stat rows are different per level.
	 * Length should match MaxLevel.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade|UI")
	TArray<FUpgradeLevelDisplay> LevelDisplays;

	/**
	 * Description shown on the card for the given level (1-indexed).
	 * Returns LevelDisplays[Level-1].Description if present and non-empty; otherwise the
	 * generic Description above as a fallback.
	 */
	UFUNCTION(BlueprintPure, Category = "Upgrade|UI")
	virtual FText GetDescriptionForLevel(int32 Level) const
	{
		if (LevelDisplays.IsValidIndex(Level - 1))
		{
			const FText& Per = LevelDisplays[Level - 1].Description;
			if (!Per.IsEmpty()) return Per;
		}
		return Description;
	}

	/**
	 * Stat rows shown on the card for the given level (1-indexed).
	 * Returns LevelDisplays[Level-1].Stats; empty array if the level isn't in the table.
	 */
	UFUNCTION(BlueprintPure, Category = "Upgrade|UI")
	virtual TArray<FUpgradeStat> GetDisplayedStats(int32 Level) const
	{
		if (LevelDisplays.IsValidIndex(Level - 1))
		{
			return LevelDisplays[Level - 1].Stats;
		}
		return TArray<FUpgradeStat>();
	}

	// UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("UpgradeDefinition"), GetFName());
	}
};
