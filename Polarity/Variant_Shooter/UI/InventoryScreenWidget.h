// InventoryScreenWidget.h
// The inventory overlay: the cell grid, the weapon attachment slots, and the ground loot panel,
// drawn over a blurred but still running game.
//
// This is the second half of the split described in Docs/Inventory_Apex_Rework_Plan_2026-08-28.md.
// The corner (UInventoryBarWidget) carries what is read while shooting; everything that has to be
// thought about is here, behind a key.
//
// Two rules this class exists to enforce:
//
//  - It NEVER pauses the game. UUpgradeChoiceWidget pauses because a level-up is a single-player
//    beat, but the inventory is opened mid-fight, and the game is co-op: one player's pause is a
//    freeze frame for the other three. Apex does not pause its inventory for the same reason.
//  - Input goes to GameAndUI, not UIOnly. Walking with the inventory open is the point; the only
//    thing given up is looking around, because the mouse is now a cursor.
//
// The blur itself is a plain UMG BackgroundBlur in the Blueprint. Nothing here draws.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Variant_Shooter/Inventory/InventoryTypes.h"
#include "Variant_Shooter/Weapons/WeaponAttachmentDefinition.h"
#include "InventoryScreenWidget.generated.h"

class AShooterCharacter;
class AShooterWeapon;
class UInventoryComponent;
class UInventorySlotWidget;
class UPanelWidget;
class UTexture2D;
class UUniformGridPanel;

/**
 * Full-screen inventory. Inherit in Blueprint (WBP_InventoryScreen), give it the containers below
 * by name, and set the two square classes and the four icons.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UInventoryScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/** Bind to the character's inventory and draw the current state. Safe to call again after a
	 *  respawn: it unbinds the previous character first. */
	UFUNCTION(BlueprintCallable, Category = "Inventory Screen")
	void InitializeFor(AShooterCharacter* InCharacter);

	/** Unbind everything. Closes the screen first if it is open, so input never stays in UI mode
	 *  with no screen to explain why. */
	UFUNCTION(BlueprintCallable, Category = "Inventory Screen")
	void Shutdown();

	UFUNCTION(BlueprintCallable, Category = "Inventory Screen")
	void Open();

	UFUNCTION(BlueprintCallable, Category = "Inventory Screen")
	void Close();

	UFUNCTION(BlueprintCallable, Category = "Inventory Screen")
	void Toggle();

	UFUNCTION(BlueprintPure, Category = "Inventory Screen")
	bool IsOpen() const { return bIsOpen; }

protected:

	virtual void NativeDestruct() override;

	/** A cell dropped on nothing in particular. That is the throw-away gesture: the item is spawned
	 *  in the world in front of the player, exactly as if it had been dropped from the grid by the
	 *  console command, and the cell is freed.
	 *
	 *  Only reached when no square handled the drop first, so a drag that lands on any cell -- even
	 *  a locked one -- never gets here. */
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;

	/** Turn a drag that ends outside the grid into a drop on the floor.
	 *
	 *  On by default because throwing things away is half of rule 2 in the contract and there is no
	 *  other gesture for it yet. Off if it ever proves too easy to do by accident: the console
	 *  command and any future dedicated bin still work. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Screen")
	bool bDropOutsideGridDropsToWorld = true;

	// ==================== Containers (must exist in the WBP under these names) ====================

	/** The cell grid. Filled two rows tall, growing sideways. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUniformGridPanel> GridContainer;

	/** Attachment squares under the first weapon. Optional: the weapon column can be built later
	 *  without holding up the grid. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> FirstWeaponAttachments;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> SecondWeaponAttachments;

	// ==================== Classes and icons (set in the WBP) ====================

	/** 74x74 square used in the grid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Screen|Classes")
	TSubclassOf<UInventorySlotWidget> GridCellClass;

	/** 48x48 square used under a weapon. Same C++ class, different Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Screen|Classes")
	TSubclassOf<UInventorySlotWidget> AttachmentSlotClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Screen|Icons")
	TObjectPtr<UTexture2D> CurrencyIcon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Screen|Icons")
	TObjectPtr<UTexture2D> AmmoIcon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Screen|Icons")
	TObjectPtr<UTexture2D> AbilityUpgradeIcon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Screen|Icons")
	TObjectPtr<UTexture2D> AttachmentIcon;

	// ==================== Blueprint hooks ====================

	/** Draw one weapon panel at the top of the screen.
	 *
	 *  Index 0 is the gun in hand and index 1 the stowed one, matching the corner. Unlike the
	 *  corner, both panels have the same shape here: the screen is read, not glanced at, and the
	 *  point of opening it is to compare the two guns and move attachments between them.
	 *
	 *  Weapon is null for an empty slot, which the Blueprint should draw as an empty holster.
	 *
	 *  ReserveAmmo and bInfiniteAmmo mean the same as on the corner: this gun's own rounds out of
	 *  the grid, and "energy weapon, the number is meaningless". */
	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory Screen", meta = (DisplayName = "Set Weapon Panel"))
	void BP_SetWeaponPanel(int32 PanelIndex, AShooterWeapon* Weapon, int32 CurrentAmmo, int32 MagazineSize,
		int32 ReserveAmmo, bool bInfiniteAmmo, bool bIsEquipped);

	/** Fired after the screen is shown and input has been handed to the cursor. Play the blur and
	 *  fade-in here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory Screen", meta = (DisplayName = "On Opened"))
	void BP_OnOpened();

	/** Fired after the screen is hidden and input has gone back to the game. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory Screen", meta = (DisplayName = "On Closed"))
	void BP_OnClosed();

private:

	void Rebuild();
	void RebuildGrid(const UInventoryComponent& Inventory);
	void RebuildWeaponPanels(const UInventoryComponent& Inventory);
	/** Draw one weapon's attachment squares and wire them up.
	 *
	 *  The squares are CAPACITY, not types: which of the four kinds an attachment is comes from the
	 *  attachment, so a part dropped on any usable square of this weapon goes to its own slot. That
	 *  is the layout in the contract -- a row of squares beside the weapon plate -- rather than four
	 *  labelled holes the player has to aim at. */
	void RebuildAttachmentSlots(UPanelWidget* Container, AShooterWeapon* Weapon, int32 FreeSlots, int32 MaxSlots);

	/** A grid cell was dropped on a weapon's square. */
	void HandleAttachmentInstall(int32 FromIndex, AShooterWeapon* Weapon);

	/** A mounted attachment was dragged off a weapon, onto a cell or onto the background. */
	void HandleAttachmentRemove(AShooterWeapon* Weapon, EWeaponAttachmentType InType, int32 ToIndex);

	/** Reuses the square already at Index in Container, or spawns one. */
	UInventorySlotWidget* GetOrCreateSquare(UPanelWidget* Container, int32 Index, TSubclassOf<UInventorySlotWidget> SquareClass);

	UTexture2D* IconForKind(EInventorySlotKind InKind) const;

	/** The picture for one cell. Falls through to IconForKind for everything except ammo, which
	 *  wears its weapon's badge instead of a generic bullet. */
	UTexture2D* IconForSlot(const FInventorySlot& InSlot) const;

	/** One square was dropped onto another. Both indices are grid cells; the component decides
	 *  whether that is a merge or a swap. */
	void HandleCellDropped(int32 FromIndex, int32 ToIndex);

	/** The one place that knows the grid is the server's: everything above it deals in indices, and
	 *  this turns an index into either a direct call or an RPC. */
	void RequestMove(int32 FromIndex, int32 ToIndex);

	void RequestDropToWorld(int32 SlotIndex);

	UFUNCTION()
	void HandleInventoryChanged();

	UFUNCTION()
	void HandleWeaponInventoryChanged();

	/** Every round fired, every reload and every weapon switch. The screen stays open while the
	 *  fight goes on, so its ammo counts have to move like the corner's do. */
	UFUNCTION()
	void HandleBulletCount(int32 MagazineSize, int32 Bullets);

	UPROPERTY(Transient)
	TWeakObjectPtr<AShooterCharacter> BoundCharacter;

	/** Squares currently in the grid, in display order. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInventorySlotWidget>> GridSquares;

	bool bIsOpen = false;
};
