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
class UUpgradeDefinition;
class UDragDropOperation;
class UWidget;

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

	/** A press on a weapon panel starts dragging the whole gun, Apex-style [author, 2026-09-29]:
	 *  onto the other panel it trades slots, anywhere that is not a panel it goes on the floor. */
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
		UDragDropOperation*& OutOperation) override;

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

	/** The whole panel of weapon slot 0 / 1: what a gun is dragged from, and what an attachment or a
	 *  gun is dropped on. Found by name; only their on-screen rectangle is used. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> WeaponPanel1;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> WeaponPanel2;

	/** The action slots (jump, aim, slide...): one labelled square per exclusive slot of the
	 *  upgrade layout, holding the equipped upgrade. Upgrades are dragged here from the grid and
	 *  back. Optional; built in C++ at runtime, the WBP only supplies the row. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> ActionSlots;

	// ==================== Classes and icons (set in the WBP) ====================

	/** 74x74 square used in the grid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Screen|Classes")
	TSubclassOf<UInventorySlotWidget> GridCellClass;

	/** 48x48 square used under a weapon. Same C++ class, different Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Screen|Classes")
	TSubclassOf<UInventorySlotWidget> AttachmentSlotClass;

	/** Square used for an action slot. Empty: the grid cell's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Screen|Classes")
	TSubclassOf<UInventorySlotWidget> ActionSlotClass;

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
	 *  One square per slot TYPE the gun has (AShooterWeapon::AttachmentSlots), in the one order the
	 *  whole game uses (UInventoryIconSettings::AttachmentSlotOrder), Apex-style [author,
	 *  2026-09-26]. An empty square shows its type's icon, dimmed; it takes only an attachment of
	 *  that type that fits the gun. The first FreeSlots mounts are free, later ones cost a cell. */
	void RebuildAttachmentSlots(UPanelWidget* Container, AShooterWeapon* Weapon, int32 FreeSlots);

	/** A grid cell was dropped on a weapon's square. */
	void HandleAttachmentInstall(int32 FromIndex, AShooterWeapon* Weapon);

	/** A mounted attachment was dragged off a weapon onto a cell (or INDEX_NONE: any cell). */
	void HandleAttachmentRemove(AShooterWeapon* Weapon, EWeaponAttachmentType InType, int32 ToIndex);

	/** A mounted attachment was dragged onto a slot of the other gun. */
	void HandleAttachmentMove(AShooterWeapon* From, EWeaponAttachmentType InType, AShooterWeapon* To);

	/** A mounted attachment was dragged out of the screen: onto the floor. */
	void RequestDropMountedAttachment(AShooterWeapon* Weapon, EWeaponAttachmentType InType);

	/** Click on a grid cell: right throws it away, left puts an attachment on a gun. */
	void HandleCellClicked(int32 GridIndex, bool bRight);

	/** Click on a fitted part: right takes it off into the bag. */
	void HandleAttachmentClicked(AShooterWeapon* Weapon, EWeaponAttachmentType InType, bool bRight);

	/** Light every weapon slot that would take what just started being dragged. */
	void HandleDragStarted(UDragDropOperation* Operation);

	/** Any drag from this screen ended, dropped or cancelled: every hint off. */
	UFUNCTION()
	void HandleDragEnded(UDragDropOperation* Operation);

	/** The gun in weapon slot 0 or 1, or null. Panels are SLOTS: they do not move when the player
	 *  switches, and they follow the keys. */
	AShooterWeapon* GetWeaponInPanel(int32 PanelIndex) const;

	/** Which weapon panel (0/1) is under a screen position, or INDEX_NONE. */
	int32 FindPanelUnder(const FVector2D& ScreenPosition) const;

	/** Every attachment square under both guns, for the drag hints. */
	void GatherAttachmentSquares(TArray<UInventorySlotWidget*>& OutSquares) const;

	/** The panel a left press landed on, until the drag starts or the press ends. */
	int32 PressedWeaponPanel = INDEX_NONE;

	/** Draw the action slots from the layout and what is equipped in each. */
	void RebuildActionSlots();

	/** A grid cell with an upgrade was dropped on its action slot. */
	void HandleUpgradeEquip(int32 FromIndex, int32 SlotIndex);

	/** An action slot's upgrade was dragged to a cell (or the background: INDEX_NONE). */
	void HandleUpgradeUnequip(int32 SlotIndex, int32 ToIndex);

	UFUNCTION()
	void HandleUpgradeChanged(UUpgradeDefinition* Definition);

	UFUNCTION()
	void HandleUpgradeLeveled(UUpgradeDefinition* Definition, int32 NewLevel);

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

	/** Action slot squares, one per exclusive slot of the layout, in layout order. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInventorySlotWidget>> ActionSquares;

	/** Layout slot index each of ActionSquares stands for (passive slots are skipped). */
	TArray<int32> ActionSquareSlotIndices;

	bool bIsOpen = false;
};
