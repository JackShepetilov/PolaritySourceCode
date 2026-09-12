// InventoryComponent.h
// The cell grid: how much the player can carry, and what is currently in it.
//
// This component is a LEDGER OF CAPACITY, not a container of actors. The weapons the player owns
// keep living in AShooterCharacter::OwnedWeapons exactly as before, and nothing here touches them.
// That is deliberate: making the grid the owner of weapons would drag weapon switching, the bullet
// counter and weapon replication into this change, and none of them need to move for the grid to
// work. The only place the two ever meet is the single question a pickup asks, "is there room",
// and that question is answered here.
//
// Authority: every mutator runs on the server, the array replicates to the owning client, and the
// HUD rebuilds from the delegate. Same shape as UAbilityComponent, including COND_OwnerOnly -- what
// a teammate is carrying is not something another player's screen has any use for.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Variant_Shooter/Inventory/InventoryTypes.h"
// For EWeaponAttachmentType in the mount/unmount signatures. UHT needs the complete enum, so this
// is an include rather than a forward declaration; the asset header itself pulls in nothing heavy.
#include "Variant_Shooter/Weapons/WeaponAttachmentDefinition.h"
#include "InventoryComponent.generated.h"

/** Fired on every machine that can see the grid, whenever anything in it changes. The HUD rebuilds
 *  the whole row from this rather than being told what changed: the grid is at most eight cells, so
 *  a diff would cost more to write and to read than it saves. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryChanged);

class AInventoryPickup;

UCLASS(ClassGroup = (Polarity), meta = (BlueprintSpawnableComponent))
class POLARITY_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UInventoryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==================== Reading ====================

	/** Every unlocked cell, in display order. Length IS the player's current capacity: cells the
	 *  meta has not bought yet are simply not here, and the HUD draws MaxSlotCount minus this
	 *  length as the dashed ones. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	const TArray<FInventorySlot>& GetSlots() const { return Slots; }

	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetSlotCount() const { return Slots.Num(); }

	/** The ceiling the meta can buy up to. The HUD needs it to draw what is still locked. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetMaxSlotCount() const { return MaxSlotCount; }

	/** Cells holding nothing at all. Does not count partial stacks, because a partial stack cannot
	 *  take an arbitrary item -- only more of the same thing. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetEmptyCellCount() const;

	/** Every round the player owns, magazines included.
	 *
	 *  ONE POOL for every weapon [author, 2026-09-01]. A weapon has no reserve field of its own, so
	 *  this is the only source for that number, and it is the reason ammo is worth a cell: carrying
	 *  a reload costs the run a cell of money. The class weapon is energy and spends none of it. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetAmmo() const;

	/** Spend rounds. Returns how many were actually taken, which is less than Count when the player
	 *  did not have that many.
	 *
	 *  Server only, like every other writer here. Called once per shot from
	 *  AShooterWeapon::SpendPooledRound, on the host directly and for a client through
	 *  AShooterCharacter::Server_ReportWeaponFired. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 ConsumeAmmo(int32 Count);

	/** How many attachments this player may mount without paying a cell for them. Per weapon, not
	 *  per player: one free slot means one free attachment on the class weapon AND one on the
	 *  looted one. Choosing which gun gets the only free slot would be a decision made mid-fight
	 *  on every weapon swap, and it pays for nothing. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetFreeAttachmentSlots() const { return FreeAttachmentSlots; }

	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetMaxFreeAttachmentSlots() const { return MaxFreeAttachmentSlots; }

	/** Units per cell for extraction currency. The pickup asks for this rather than carrying its
	 *  own copy, so the stack size stays one number in one place. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetCurrencyStackSize() const { return CurrencyStackSize; }

	/** Could this be taken, and where would the first of it land.
	 *
	 *  Safe to call on any machine: it reads the replicated array and nothing else, which is what
	 *  lets a pickup grey itself out on the client without a round trip. The server checks again
	 *  when it actually takes it. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool CanAccept(const FInventoryItem& Item, int32& OutSlotIndex) const;

	// ==================== Writing (server) ====================

	/** Take as much of Item as fits, topping up partial stacks of the same thing before opening a
	 *  new cell. Returns the number of units that did NOT fit, so 0 means everything was taken and
	 *  the pickup can destroy itself, while anything else means it should stay and shrink.
	 *
	 *  Partial pickups exist because currency arrives in 15-60 unit drops against a stack of 100:
	 *  a cell that only has room for part of a drop should take that part rather than refuse the
	 *  whole thing. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 TryAdd(const FInventoryItem& Item);

	/** Empty one cell and put NOTHING in the world. This is the destructive one: it is what the
	 *  debug command and the "the weapon left, so did its rounds" paths want. To throw something
	 *  away where the player can pick it up again, use DropSlotToWorld. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool ClearSlot(int32 SlotIndex);

	/** Move a cell onto another one: merge when they are the same stackable thing, swap otherwise.
	 *
	 *  A merge can be partial, and the remainder stays where it was rather than being lost -- two
	 *  half stacks of money poured together leave one full cell and one with the rest, which is the
	 *  same arithmetic TryAdd does for a pickup.
	 *
	 *  Server only. The cursor on a client asks through Server_MoveSlot. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool MoveSlot(int32 FromIndex, int32 ToIndex);

	/** Drag and drop in the overlay. The grid is the server's, so a client's cursor asks instead of
	 *  writing: the array it is looking at is a replicated copy, and a local edit would be undone
	 *  by the next update from the server anyway. */
	UFUNCTION(Server, Reliable)
	void Server_MoveSlot(int32 FromIndex, int32 ToIndex);

	/** Throw a cell away: spawn DroppedItemClass in front of the owner holding what was in it, then
	 *  empty the cell.
	 *
	 *  This is the other half of rule 2 in the contract -- what costs a cell can be dropped and
	 *  picked up, and that IS the trade system between players. It is also the reason
	 *  AInventoryPickup carries a plain FInventoryItem: a dropped cell of rounds and a dropped cell
	 *  of money are the same actor with different contents.
	 *
	 *  Server only; clients ask through Server_DropSlotToWorld. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool DropSlotToWorld(int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void Server_DropSlotToWorld(int32 SlotIndex);

	/** Grow or shrink capacity. Shrinking discards the cells that fall off the end, so the meta
	 *  must never call this downwards on a live run. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SetSlotCount(int32 NewCount);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SetFreeAttachmentSlots(int32 NewCount);

	// ==================== Magazines ====================
	//
	// A cell of ammo is a MAGAZINE, not a number of rounds. One cell holds up to the weapon's
	// magazine size and costs the same capacity whether it is full or nearly empty, which is why
	// the count below is in magazines rather than in rounds.
	//
	// The rounds in the gun are part of this: with one magazine allowed, the player carries exactly
	// what is loaded and there is nothing to reload from. The upgrade that raises this to two is
	// what makes reloading mean anything.

	/** How many cells of ammo may exist for ONE weapon. Raised by the meta. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetMaxAmmoCells() const;

	/** Rounds one cell of ammo holds. Public because a pickup deciding what it is worth, and a
	 *  weapon drop deciding how its rounds stack, both have to ask before anyone owns them. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetRoundsPerAmmoCell() const { return FMath::Max(1, RoundsPerAmmoCell); }

	/** How many cells are currently spent on this weapon's ammo. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetAmmoCellCount() const;

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SetMaxAmmoCells(int32 NewCount);

	/** Drop every round the player holds for one weapon and report how many there were.
	 *
	 *  Used when the weapon itself leaves: its ammo goes with it, so the number comes back to be
	 *  written onto the drop, and the cells are freed in the same breath. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 TakeAllAmmo();

	// ==================== Attachments ====================
	//
	// The WEAPON records what is fitted (AShooterWeapon::InstalledAttachments). This component only
	// decides who PAYS for it, and that is the whole of the mechanic: the first FreeAttachmentSlots
	// parts on a gun are free, and every one past that keeps holding the cell it came out of.
	//
	// Which of them is "past that" is never stored. It is recomputed from the two numbers each time
	// anything changes, because a stored answer is one that can disagree with the gun.

	/** Mount the attachment in SlotIndex onto Weapon.
	 *
	 *  Frees the cell when the weapon still has a free slot, and keeps it -- marked installed and
	 *  pointing at that weapon -- when it does not. Server only; a client asks through
	 *  Server_InstallAttachmentFromSlot. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool InstallAttachmentFromSlot(int32 SlotIndex, AShooterWeapon* Weapon);

	UFUNCTION(Server, Reliable)
	void Server_InstallAttachmentFromSlot(int32 SlotIndex, AShooterWeapon* Weapon);

	/** Take an attachment off a weapon and put it back in the bag.
	 *
	 *  A part that was paying for a cell simply stops being installed and stays in that same cell.
	 *  A part that was mounted for free needs a cell to come back to, and if there is none the
	 *  removal is REFUSED rather than silently destroying it: a full bag is a reason to be told no,
	 *  not a reason to lose an attachment. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool UninstallAttachment(AShooterWeapon* Weapon, EWeaponAttachmentType InType);

	UFUNCTION(Server, Reliable)
	void Server_UninstallAttachment(AShooterWeapon* Weapon, EWeaponAttachmentType InType);

	/** How many cells this weapon's attachments are currently holding. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetHeldAttachmentCellCount(const AShooterWeapon* Weapon) const;

	/** The cell holding this weapon's attachment of that type, or INDEX_NONE when that attachment
	 *  is mounted for free and holds no cell at all. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 FindHeldAttachmentCell(const AShooterWeapon* Weapon, EWeaponAttachmentType InType) const;

	/** Release held cells that the free slots now cover.
	 *
	 *  Called after anything that can change either side of the sum: a part mounted or removed, and
	 *  the meta raising the free slot count. Releasing a cell does not lose the attachment -- the
	 *  weapon is what holds it -- it only stops the player paying for it. */
	void RebalanceAttachmentCells(AShooterWeapon* Weapon);

	/** Stop paying for every attachment on this weapon. Used when the weapon leaves the player:
	 *  attachments travel with the gun, so the cells they held come back empty. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ReleaseAttachmentCellsFor(const AShooterWeapon* Weapon);

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FOnInventoryChanged OnInventoryChanged;

protected:

	virtual void BeginPlay() override;

	/** Unlocked cells. Owner-only: a teammate's bag is not information this screen can use, and
	 *  four players' worth of grids would otherwise replicate to everyone for nothing. */
	UPROPERTY(ReplicatedUsing = OnRep_Slots)
	TArray<FInventorySlot> Slots;

	UPROPERTY(ReplicatedUsing = OnRep_Slots)
	int32 FreeAttachmentSlots = 1;

	/** Magazines the player may carry per weapon. One until the meta buys the second. */
	UPROPERTY(ReplicatedUsing = OnRep_Slots)
	/** How many cells of ammo the player may open. Was "magazines per weapon" while ammo was keyed
	 *  to a gun; with one pool it is simply how many cells of rounds fit in the bag. */
	int32 MaxAmmoCells = 2;

	/** Rounds in one cell. One number now that every gun eats the same rounds; it used to be that
	 *  gun's magazine size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "1"))
	int32 RoundsPerAmmoCell = 60;

	UFUNCTION()
	void OnRep_Slots();

	// ==================== Configuration ====================

	/** Cells the player starts a run with, before the meta has bought anything. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "1"))
	int32 StartingSlotCount = 4;

	/** The ceiling. Reaching it is what opens the final mission, so it is a design number rather
	 *  than a technical one.
	 *
	 *  It is now drawn as well as counted: the overlay shows every cell up to here, padlocked until
	 *  the meta buys it, the way Apex shows the rows a backpack would open. That makes this number
	 *  visible to the player, so it is tuned here on the component rather than assumed anywhere. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "1"))
	int32 MaxSlotCount = 10;

	/** Free attachment slots at the start of the meta, per weapon. One rather than zero on purpose:
	 *  an attachment and an ability upgrade cost the same cell but are worth wildly different
	 *  things, so at equal price the upgrade wins every time and attachments would become loot
	 *  nobody picks up. The first one being free is what keeps them worth taking. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "1"))
	int32 StartingFreeAttachmentSlots = 1;

	/** One per attachment type: sight, magazine, muzzle, stock. When the meta has bought all four,
	 *  a weapon is fully kitted for free -- which is the natural end of that progression line. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "1"))
	int32 MaxFreeAttachmentSlots = 4;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "1"))
	int32 CurrencyStackSize = 100;

	/** What a thrown-away cell becomes when the cell itself has no answer.
	 *
	 *  It usually does have one: a cell filled by a pickup remembers that pickup and drops exactly
	 *  it, so the money the player found goes back on the floor as that money and not as a stand-in
	 *  (FInventorySlot::PickupClass). This map is for the cells that were never filled by a pickup
	 *  at all -- rounds handed over by a weapon drop, anything a console command put there.
	 *
	 *  Leave a kind out and dropping that kind is simply refused, in the log, with the cell kept. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	TMap<EInventorySlotKind, TSubclassOf<AInventoryPickup>> DropClassByKind;

	/** Last resort, for a kind that is not in the map either. Optional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<AInventoryPickup> DroppedItemClass;

	/** How far in front of the eyes a thrown-away cell lands. Far enough not to clip the player,
	 *  near enough to be grabbed straight back when the drop was a mistake. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "0.0", Units = "cm"))
	float DropForwardDistance = 150.0f;

private:

	/** Index of a partial stack that would accept more of Item, or INDEX_NONE. */
	int32 FindStackWithRoom(const FInventoryItem& Item) const;

	/** Index of the first cell holding nothing, or INDEX_NONE. */
	int32 FindEmptyCell() const;

	/** True when a cell can merge with this item: same kind and same definition. Currency has no
	 *  definition, so all currency merges with all currency. */
	static bool CanMerge(const FInventorySlot& Slot, const FInventoryItem& Item);

	/** The same question between two cells, for drag and drop. */
	static bool CanMergeSlots(const FInventorySlot& Into, const FInventorySlot& From);

	/** Where a thrown-away cell should land, in world space. */
	FTransform GetDropTransform() const;
};
