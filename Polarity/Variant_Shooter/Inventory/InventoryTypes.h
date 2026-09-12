// InventoryTypes.h
// What one cell of the inventory grid can hold.
//
// The contract these types encode lives in Docs/Inventory_Slot_Contract_2026-08-28.md. Two parts of
// it are worth repeating here because they are the reason the shapes below look the way they do:
//
//  - One cell holds one thing. There is no geometry, no multi-cell items, no rotation. A cell is a
//    unit of capacity, not a place, so nothing in here carries a position.
//  - Weapons are deliberately absent from the enum. Both weapons have their own place on the HUD
//    beside the grid, the way Apex keeps two weapon slots out of the backpack, so a weapon costs
//    the grid nothing. What a weapon does cost the grid is spare magazines and every attachment
//    past the free ones.

#pragma once

#include "CoreMinimal.h"
#include "InventoryTypes.generated.h"

class AShooterWeapon;
class AInventoryPickup;

/** What is in a cell. */
UENUM(BlueprintType)
enum class EInventorySlotKind : uint8
{
	/** Unlocked and holding nothing. A cell the meta has NOT unlocked is not in the array at all:
	 *  the grid is exactly as long as the player's capacity, and the HUD draws the difference
	 *  between that length and the maximum as the dashed cells. */
	Empty,

	/** Extraction currency. Stacks, and fills a cell gradually rather than in one lump, which is
	 *  what keeps "a little more would still fit" true almost all the time. */
	Currency,

	/** A spare magazine for the looted weapon. Stacks. The magazine currently IN a weapon is not
	 *  here; it belongs to the weapon and is drawn on the weapon plate. */
	Ammo,

	/** Ability upgrade. Never stacks. This is the thing that changes hands when the team splits
	 *  into couriers and fighters, so it has to be an ordinary droppable item like everything else. */
	AbilityUpgrade,

	/** Weapon attachment. See bInstalled on the slot: a loose one is simply being carried, an
	 *  installed one is already mounted on a weapon and is still holding this cell because the
	 *  player's free attachment slots are used up. */
	Attachment
};

/**
 * One cell as it replicates.
 *
 * Payload is a UObject rather than a typed pointer because the four kinds point at four different
 * things and a union of them would only move the branch somewhere less obvious. Currency carries no
 * payload at all: a unit of money is a number, not an asset.
 */
USTRUCT(BlueprintType)
struct FInventorySlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	EInventorySlotKind Kind = EInventorySlotKind::Empty;

	/** The item's definition asset. Null for Currency, and null for an empty cell. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UObject> Payload = nullptr;

	/** How many units are in the cell. Always 1 for the kinds that do not stack. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 Count = 0;

	/** How many units fit. Equal to Count for the kinds that do not stack, which is what lets the
	 *  HUD fill every cell by the same Count/StackMax ratio without asking what is in it. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 StackMax = 0;

	/** Attachment only. True when it is mounted on a weapon and paying for this cell anyway.
	 *  The HUD draws these as a dim ghost of the same icon that is shown on the weapon, which is
	 *  what tells the player why the bag is full without drawing a connector. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	bool bInstalled = false;

	/** Attachment only, and only while bInstalled: the weapon it is fitted to.
	 *
	 *  A pointer to the weapon rather than an index into the character's weapon list. That list
	 *  shifts the moment a gun is dropped, and every held cell would then be paying for a part on
	 *  the wrong weapon -- a bug that only shows up two weapon swaps later. The weapon replicates,
	 *  so the pointer resolves on the owning client as well.
	 *
	 *  The cell holds no copy of the attachment itself: the definition it points at through Payload
	 *  is the same asset the weapon has in its own list, so the two records cannot disagree about
	 *  what is fitted. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<AShooterWeapon> InstalledOnWeapon = nullptr;

	// Ammo used to be bound to a weapon class here, and every cell of it was rounds for one
	// specific gun. It is now ONE POOL for every weapon [author, 2026-09-01]: a round is a round,
	// and a cell of ammo feeds whatever is in hand. Nothing keys ammo to a gun any more.

	/** The actor this came into the bag as, so throwing it away puts THAT back on the floor.
	 *
	 *  A cell remembering its own pickup is the difference between dropping the money you found and
	 *  dropping a nameless placeholder that happens to hold a number. Nothing else can answer it:
	 *  the kind says "currency", not "this crate of currency", and there is no definition asset
	 *  behind money to hang the answer on.
	 *
	 *  Null for anything that never arrived as a pickup -- rounds handed over by a weapon drop, a
	 *  cell filled by a console command -- and the component falls back to its per-kind classes for
	 *  those. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<AInventoryPickup> PickupClass = nullptr;

	bool IsEmpty() const { return Kind == EInventorySlotKind::Empty; }

	/** Currency and ammo merge into a partial cell of the same thing; the others never do. */
	bool IsStackable() const
	{
		return Kind == EInventorySlotKind::Currency || Kind == EInventorySlotKind::Ammo;
	}

	bool HasRoom() const { return !IsEmpty() && IsStackable() && Count < StackMax; }

	/** Out of line, in InventoryTypes.cpp, and it has to stay there.
	 *
	 *  Comparing two TSubclassOf goes through its conversion to UClass*, which validates the class
	 *  against T::StaticClass() and therefore needs AShooterWeapon to be COMPLETE, not just
	 *  forward-declared. Written inline, this header compiled only by luck: it broke the moment a
	 *  new file shuffled the unity blobs and it landed next to a translation unit that had only the
	 *  forward declaration. One .cpp that includes the weapon header is the whole fix, and it keeps
	 *  this header light for the widgets and pickups that include it. */
	bool operator==(const FInventorySlot& Other) const;
};

/**
 * What is being offered to the inventory.
 *
 * Separate from FInventorySlot on purpose: a pickup knows what it is and how much of it there is,
 * but it has no business knowing which cell it will land in or whether it will be split across two.
 */
USTRUCT(BlueprintType)
struct FInventoryItem
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	EInventorySlotKind Kind = EInventorySlotKind::Empty;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	TObjectPtr<UObject> Payload = nullptr;

	/** Units on offer. 1 for everything that does not stack. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "1"))
	int32 Count = 1;

	/** Units per cell. 1 for everything that does not stack, so one item takes exactly one cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "1"))
	int32 StackMax = 1;

	/** Which pickup actor this arrived as. Filled in by AInventoryPickup as it hands itself over,
	 *  and carried into the cell so the cell can put the same thing back. See
	 *  FInventorySlot::PickupClass. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<AInventoryPickup> PickupClass = nullptr;

	bool IsValid() const { return Kind != EInventorySlotKind::Empty && Count > 0 && StackMax > 0; }

	bool IsStackable() const
	{
		return Kind == EInventorySlotKind::Currency || Kind == EInventorySlotKind::Ammo;
	}
};
