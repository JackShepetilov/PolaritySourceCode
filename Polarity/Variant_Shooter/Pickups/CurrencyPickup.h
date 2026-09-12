// CurrencyPickup.h
// Extraction money lying on the floor.
//
// The first child of AInventoryPickup, and the reason that class is shaped the way it is: a drop is
// 15-60 units against a stack of 100, so a pile is very often taken in part. Everything about
// partial takes lives in the parent; all this class adds is "how much" and "the stack size is the
// player's, not mine".
//
// Currency is the item the whole grid is a dilemma about (Docs/Inventory_Slot_Contract_2026-08-28.md
// section 6): a cell of money is a cell not carrying an attachment or an ability upgrade.

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Pickups/InventoryPickup.h"
#include "CurrencyPickup.generated.h"

class UInventoryComponent;

UCLASS(Blueprintable)
class POLARITY_API ACurrencyPickup : public AInventoryPickup
{
	GENERATED_BODY()

public:

	ACurrencyPickup();

	/** Units in this pile. The contract's drop range is 15-60 against a stack of 100, which is what
	 *  makes "a bit more would still fit" true almost every time. Placed by hand for now: where
	 *  money spawns is a separate job and it is deliberately not started here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Currency", meta = (ClampMin = "1"))
	int32 Amount = 30;

protected:

	virtual void BeginPlay() override;

	/** Amount is the authored number and BeginPlay seeds the item from it, so a pile spawned from a
	 *  thrown-away cell has to move Amount too. Without this the drop would come back holding the
	 *  Blueprint's default, which is the same bug as forgetting to pass the count at all. */
	virtual void InitializeSpawnedItem(const FInventoryItem& InItem) override;

	/** Stack size comes from the grid being filled, not from this actor. */
	virtual FInventoryItem MakeItemFor(const UInventoryComponent& Inventory) const override;
};
