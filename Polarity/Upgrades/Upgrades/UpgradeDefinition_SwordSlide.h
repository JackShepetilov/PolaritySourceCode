// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_SwordSlide.generated.h"

/**
 * Slide-slot upgrade (DA_SwordSlide): shots fired while sliding come out of the reserve instead of
 * the magazine. Reference: Axle's Sliding Shooter in Apex Legends (season 29), "bullets shot while
 * sliding come straight from inventory, up to 50% of the magazine". The class keeps its old name so
 * the asset keeps working; it no longer has anything to do with swords or damage.
 */
USTRUCT(BlueprintType)
struct FSwordSlideLevelData
{
	GENERATED_BODY()

	/** How many shots per slide may come out of the reserve, as a share of the magazine. Apex: 0.5.
	 *  Rounded down, at least one. Counted again from zero on every new slide. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sliding Shooter", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float MagazineFraction = 0.5f;

	/** The slide has to be at least this fast for a shot to count. Apex: 200 in its units, times the
	 *  project's 1.96 for its own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sliding Shooter", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinSlideSpeed = 392.0f;
};

UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_SwordSlide : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	/** One entry per level (index 0 = Lv 1). Length defines MaxLevel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sliding Shooter")
	TArray<FSwordSlideLevelData> LevelData;

	/** Data for Level (1-based); the last entry past the end, defaults when empty. */
	const FSwordSlideLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
