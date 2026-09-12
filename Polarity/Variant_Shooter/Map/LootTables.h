// LootTables.h
// The two data assets the loot generator reads.
//
//   ULootPool     - everything of ONE category that exists, each line with a Value and a weight.
//   ULootRegistry - the one asset the world points at: the pools, plus how big a sheet is.
//
// There is no tier asset. A place is a number (see APoiActor::LootQuality), so Basic / Mid / High
// would only be three presets of that number kept in three files.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Variant_Shooter/Map/LootTypes.h"
#include "LootTables.generated.h"

/**
 * Everything of one category that can come up, ordered on the Value axis.
 *
 * A pool is not a list of what a place holds. It is the whole game's answer for that category, and
 * the roll decides which end of it a given place sees.
 */
UCLASS(BlueprintType)
class POLARITY_API ULootPool : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	/** Which category this pool answers for. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot Pool")
	ELootCategory Category = ELootCategory::Ammo;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot Pool")
	TArray<FPoiLootEntry> Entries;

	/** How far from the asked-for value a line can still come up. Small means a place hands out
	 *  exactly what it is worth and nothing else; large means the value is a suggestion.
	 *
	 *  This is the knob that stops a point being the same point twice: at zero tolerance the best
	 *  line near a quality always wins, and every High point in the map holds the same thing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot Pool", meta = (ClampMin = "0.01"))
	float MatchTolerance = 0.15f;

	/** A line near Value, by weight and by closeness. Falls back to the nearest line by value alone
	 *  when nothing is close enough, because an empty slot reads as a bug rather than as bad luck. */
	bool PickEntry(float Value, FPoiLootEntry& OutEntry) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("LootPool"), GetFName());
	}
};

/**
 * The one asset the world points at.
 *
 * A map that wants different economics swaps ONE reference rather than re-pointing every point on
 * it. Set in DefaultGame.ini under ULootGeneratorSubsystem.
 */
UCLASS(BlueprintType)
class POLARITY_API ULootRegistry : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot Registry")
	TArray<TObjectPtr<ULootPool>> Pools;

	/** Which categories can come up at all, and how often against each other. Global rather than
	 *  per place: what a place is worth is its Quality, not a different diet. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot Registry")
	TMap<ELootCategory, float> CategoryWeights;

	/** How many things a sheet holds, at Quality 0 and at Quality 1. Interpolated, then jittered by
	 *  SlotJitter. A rich place is bigger as well as better, which is most of why it is worth
	 *  crossing a map for. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot Registry", meta = (ClampMin = "0"))
	int32 SlotsAtZeroQuality = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot Registry", meta = (ClampMin = "0"))
	int32 SlotsAtFullQuality = 6;

	/** Plus or minus this many slots, so two points of the same quality are not the same size. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot Registry", meta = (ClampMin = "0"))
	int32 SlotJitter = 1;

	ULootPool* GetPool(ELootCategory Category) const;

	/** How many slots a place of this quality rolls. */
	int32 RollSlotCount(float Quality) const;

	bool PickCategory(ELootCategory& OutCategory) const;

	/** Says out loud what is missing: a category weighted above zero with no pool behind it, a pool
	 *  with no entries, a pool with a hole in its value range. Called once when the generator loads
	 *  it, because a hole here shows up as "the map has no loot" three systems away. */
	void LogGaps() const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("LootRegistry"), GetFName());
	}
};
