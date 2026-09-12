// LootAnchor.h
// A spot on a point where a loot sheet MIGHT be.
//
// Anchors are placed by hand, because where a stash reads well is a level design question and not
// something a radius can answer: the corner of a shed, the lee of a wall, the middle of a courtyard
// somebody has to cross. What sits on the anchor is a roll, so the same blockout plays differently
// twice - the anchor is the place, the sheet is the prize, and the chance is why anyone sprints.
//
// The roll happens once per run and is remembered by the director, so a point that streams out and
// back does not refill itself (see ALootSheet for what lands here).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LootAnchor.generated.h"

class ALootSheet;
class APoiActor;
class USphereComponent;

/** One kind of sheet this anchor can produce, and how often relative to the others. */
USTRUCT(BlueprintType)
struct POLARITY_API FLootSheetOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot")
	TSubclassOf<ALootSheet> SheetClass = nullptr;

	/** Relative weight against the other options. Two entries at 1 and 3 are a quarter and three
	 *  quarters; the numbers mean nothing on their own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;
};

UCLASS()
class POLARITY_API ALootAnchor : public AActor
{
	GENERATED_BODY()

public:

	ALootAnchor();

	/** Identity of this anchor across the run, so the roll survives the level streaming out and in.
	 *  Left empty it falls back to the actor's name, which is already unique inside a level; set it
	 *  when you want a readable name in the log. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Anchor")
	FName AnchorTag;

	/** Odds a sheet appears here at all, 0..1. This is the whole reason an anchor exists: an anchor
	 *  that always pays is just a pile of loot with extra steps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Anchor", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Chance = 0.6f;

	/** Which sheets can land here, and how often each. Empty means the anchor is decoration.
	 *
	 *  A sheet class is art and shape, NOT contents: what ends up on it is rolled from the point's
	 *  quality. Two entries here are two different-looking stashes, not a rich one and a poor one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Anchor")
	TArray<FLootSheetOption> Sheets;

	/** The point this anchor belongs to, for the quality of what lands here. Left empty it finds
	 *  the nearest point that contains it, which is what "anchors are placed inside a point" means
	 *  in the level: drag one in only to override that. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Anchor")
	TObjectPtr<APoiActor> OwningPoi;

	/** What actually landed, or null when the roll came up empty. */
	UFUNCTION(BlueprintPure, Category = "Loot Anchor")
	ALootSheet* GetSpawnedSheet() const { return SpawnedSheet.Get(); }

	/** The name this anchor is remembered under. */
	UFUNCTION(BlueprintPure, Category = "Loot Anchor")
	FName GetAnchorKey() const;

protected:

	virtual void BeginPlay() override;

	/** Roll the chance, pick a sheet by weight, put it down. Server only, once per run. */
	void RollOnce();

	/** Picks by weight. Null when nothing is configured or every weight is zero. */
	TSubclassOf<ALootSheet> PickSheetClass() const;

	/** OwningPoi, or the nearest point whose influence reaches this anchor. */
	APoiActor* ResolvePoi() const;

	/** Visible while placing, invisible in game. An anchor with no sheet on it has nothing else to
	 *  show a designer where it is. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> AnchorGizmo;

	UPROPERTY(Transient)
	TWeakObjectPtr<ALootSheet> SpawnedSheet;
};
