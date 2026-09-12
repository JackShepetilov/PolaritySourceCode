// LootSheet.h
// A mat with things laid out on it.
//
// Loot used to be scattered over a point, or thrown out of a banner, and either way it was a
// handful of objects on the floor with nothing tying them together. A sheet is the Apex answer: one
// readable object at a distance that resolves into a set of choices when you stand over it.
//
// What lies on it is NOT configured here. The sheet is told how good the place around it is and
// asks ULootGeneratorSubsystem for that many things of about that quality [author, 2026-09-02].
// The one exception is Items: filled in, it overrides the roll entirely, which is Apex's rule for
// the few small places that carry their own table and ignore the zone they stand in.
//
// Where sheets appear is not this class's business either: see ALootAnchor.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Variant_Shooter/Map/LootTypes.h"
#include "LootSheet.generated.h"

class UStaticMeshComponent;

UCLASS()
class POLARITY_API ALootSheet : public AActor
{
	GENERATED_BODY()

public:

	ALootSheet();

	/** How good the place around this sheet is, 0..1. Set by the anchor from its point; editable
	 *  for a sheet dragged into a level on its own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot Sheet", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Quality = 0.35f;

	/** How far a single thing can stray from that, 0..1. Zero makes a place hand out exactly what
	 *  it is worth every time, which is a place nobody has a reason to search twice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot Sheet", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Spread = 0.15f;

	/** THE OVERRIDE. Empty is the normal case and means "roll for it".
	 *
	 *  Filled in, the roll is skipped and exactly these things are laid out, ignoring Quality
	 *  entirely - Apex's special lootable areas, the ones that hand out the same thing every match
	 *  wherever they happen to be. Use it for a scripted stash, not for ordinary loot: a level full
	 *  of these is the hand-written system this one replaced. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Sheet|Override")
	TArray<FPoiLootEntry> Items;

	/** Half the width of the area items are laid out in (cm). Items land inside it, never off the
	 *  edge, which is what keeps a sheet reading as one object rather than as a mess around one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Sheet", meta = (ClampMin = "20.0"))
	float LayoutExtent = 90.0f;

	/** How far above the mat an item starts before it settles (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Sheet", meta = (ClampMin = "0.0"))
	float DropHeight = 25.0f;

	/** Told to the sheet by its anchor before it begins play. */
	void SetQuality(float InQuality, float InSpread);

	/** Everything currently lying on it. */
	UFUNCTION(BlueprintPure, Category = "Loot Sheet")
	TArray<AActor*> GetLaidOut() const { return LaidOut; }

	/** Money stacks this sheet put on the floor, for the director's budget audit. Known only after
	 *  it has laid out, because until then nobody has rolled. */
	UFUNCTION(BlueprintPure, Category = "Loot Sheet")
	int32 GetMoneyStacks() const { return MoneyStacks; }

	/** The best thing that landed here, 0..1. What a marker or a ping would colour itself by. */
	UFUNCTION(BlueprintPure, Category = "Loot Sheet")
	float GetBestValue() const { return BestValue; }

	UFUNCTION(BlueprintPure, Category = "Loot Sheet")
	ELootBand GetBestBand() const { return PolarityLoot::BandForValue(BestValue); }

protected:

	virtual void BeginPlay() override;

	/** Roll (or read the override) and put the things down. Server only: pickups are replicated
	 *  from here like every other pickup. */
	void LayOutItems();

	/** One line, placed at one spot on the mat. */
	AActor* PlaceOne(const FPoiLootEntry& Entry, float AmountScale, int32 SlotIndex, int32 SlotTotal);

	/** The mat. A Blueprint subclass replaces the mesh; the layout does not care what it looks
	 *  like, only how big LayoutExtent says the usable part is. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mat;

	UPROPERTY(Transient)
	TArray<AActor*> LaidOut;

	UPROPERTY(Transient)
	int32 MoneyStacks = 0;

	UPROPERTY(Transient)
	float BestValue = 0.0f;
};
