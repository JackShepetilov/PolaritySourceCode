// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SkillTypes.h"
#include "UpgradeOfferSchedule.generated.h"

/**
 * Data-driven upgrade offer routing by run level.
 *
 * Example:
 *   1 -> Boring
 *   2 -> Fun
 *   3 -> Weapons
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeOfferSchedule : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Exact level -> upgrade category to offer. Levels are 1-based. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade Offer Schedule")
	TMap<int32, ESkillCategory> LevelToCategory;

	/** Used for levels not present in LevelToCategory when bUseAllCategoriesWhenLevelMissing is false. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade Offer Schedule")
	ESkillCategory DefaultCategory = ESkillCategory::Fun;

	/** If true, levels absent from LevelToCategory roll from every category. If false, they use DefaultCategory. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade Offer Schedule")
	bool bUseAllCategoriesWhenLevelMissing = true;

	/** If the scheduled category has no available upgrades, roll from every category instead of skipping the level. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade Offer Schedule")
	bool bFallbackToAllCategoriesWhenFilteredPoolEmpty = true;

	/** Returns true when this level should be filtered to OutCategory; false means roll all categories. */
	UFUNCTION(BlueprintPure, Category = "Upgrade Offer Schedule")
	bool TryGetCategoryForLevel(int32 Level, ESkillCategory& OutCategory) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("UpgradeOfferSchedule"), GetFName());
	}
};
