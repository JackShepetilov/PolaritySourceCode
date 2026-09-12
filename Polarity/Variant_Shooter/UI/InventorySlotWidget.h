// InventorySlotWidget.h
// One square of the inventory HUD.
//
// The same class serves both squares in the layout: a grid cell and an attachment slot beside a
// weapon. They hold different things but they answer the same four questions - what state am I in,
// what kind of thing is in me, what icon does it use, and how full am I - so one C++ class with two
// Blueprint subclasses is cheaper than two of everything. This mirrors UBarEntryWidget, where the
// ability, dash and count entries are all one class with different WBPs.
//
// C++ owns the state, Blueprint owns the look. Nothing here draws.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "Variant_Shooter/Inventory/InventoryTypes.h"
#include "Variant_Shooter/Weapons/WeaponAttachmentDefinition.h"
#include "InventorySlotWidget.generated.h"

class UImage;
class UProgressBar;
class UTextBlock;
class AShooterWeapon;
class UDragDropOperation;
class UTexture2D;

/** One cell was dropped onto another. From and To are grid indices; the screen turns them into the
 *  server call, because the square itself has no business knowing about inventories or networks. */
DECLARE_DELEGATE_TwoParams(FOnInventoryCellDropped, int32 /*FromIndex*/, int32 /*ToIndex*/);

/** A grid cell was dropped on a weapon's attachment square: fit what is in that cell to that gun. */
DECLARE_DELEGATE_TwoParams(FOnInventoryAttachmentInstall, int32 /*FromIndex*/, AShooterWeapon* /*Weapon*/);

/** A mounted attachment was dragged off a weapon. The square it landed on says where it should go,
 *  or INDEX_NONE for "anywhere it fits", which is what a drop on empty screen means. */
DECLARE_DELEGATE_ThreeParams(FOnInventoryAttachmentRemove, AShooterWeapon* /*Weapon*/,
	EWeaponAttachmentType /*Type*/, int32 /*ToIndex*/);

/** How the square should read. Independent of what is in it: an empty grid cell and an empty
 *  attachment slot are the same state wearing two different Blueprints. */
UENUM(BlueprintType)
enum class EInventoryCellVisual : uint8
{
	/** Unlocked and holding nothing. */
	Empty,

	/** Holding something. Kind, icon and fill say what. */
	Filled,

	/** The ghost. An attachment that is mounted on a weapon and still paying for this cell because
	 *  the free attachment slots are used up. Draws the SAME icon as the one shown on the weapon,
	 *  dimmed and over hatching: that shared icon is what tells the player why the bag is full,
	 *  and it is the reason no connector line is drawn between the two. */
	HeldByAttachment,

	/** The meta has not bought this square yet. Dashed and hatched, and always visible, because a
	 *  cell the player can see but not use is the only advertisement the hub needs. */
	Locked,

	/** An attachment slot beyond the free ones: usable, but mounting here costs a cell out of the
	 *  bag. Outlined rather than filled, per the contract's "залитые бесплатные, обведённые
	 *  платные".
	 *
	 *  Appended, never inserted. The values are saved inside Blueprints, so putting a new one in
	 *  the middle would silently repoint every existing switch at the wrong state. */
	Paid
};

/**
 * One square. Inherit in Blueprint twice: once for a 74x74 grid cell, once for a 48x48 attachment
 * slot beside a weapon.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UInventorySlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/** Push everything the square needs in one call.
	 *
	 *  One call rather than a setter each, because a square never changes one thing at a time: it
	 *  is rebuilt wholesale from a replicated array whenever anything about the inventory moves. */
	void SetCell(EInventoryCellVisual InVisual, EInventorySlotKind InKind, UTexture2D* InIcon,
		int32 InCount, int32 InStackMax);

	/** Fill the square straight from an inventory slot. Chooses Visual on its own from the slot's
	 *  own contents, which keeps that mapping in one place instead of at every call site.
	 *  Named InSlot rather than Slot because UWidget already has a member called Slot. */
	void SetFromSlot(const FInventorySlot& InSlot, UTexture2D* InIcon);

	UFUNCTION(BlueprintPure, Category = "Inventory Slot")
	EInventoryCellVisual GetVisual() const { return Visual; }

	// ==================== Drag and drop ====================
	//
	// A square only takes part in dragging once it has been told which cell of the grid it IS.
	// Squares that stand for something other than a grid cell -- the attachment slots beside a
	// weapon -- are never given an index, so they neither start a drag nor accept one, and that is
	// the whole gate: no separate "is this draggable" flag to keep in step with reality.

	/** Tell the square which cell it draws. INDEX_NONE opts it out of drag and drop entirely. */
	void SetGridIndex(int32 InIndex) { GridIndex = InIndex; }

	int32 GetGridIndex() const { return GridIndex; }

	/** Tell the square it is an attachment slot on a weapon, and what is currently in it.
	 *
	 *  This is the second identity a square can have, and it is the mirror of the grid index: a
	 *  square is either a cell of the bag or a slot on a gun, never both. Mounted is what is fitted
	 *  in it right now, or null for an empty slot, and it is what decides whether this square can
	 *  START a drag -- an empty slot has nothing to take off. */
	void SetAttachmentTarget(AShooterWeapon* InWeapon, UWeaponAttachmentDefinition* Mounted);

	/** Bound by the screen. Fired on the square that was dropped ON, with the dragged square's
	 *  index first. */
	FOnInventoryCellDropped OnCellDropped;

	/** Fired on a weapon's attachment square when a grid cell is dropped on it. */
	FOnInventoryAttachmentInstall OnAttachmentInstall;

	/** Fired on the square a mounted attachment was dragged onto, which may be a grid cell or the
	 *  screen background. */
	FOnInventoryAttachmentRemove OnAttachmentRemove;

protected:

	// ==================== Slate input ====================

	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
		UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;
	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	/** The square is under a dragged cell, or is no longer. Drawn in Blueprint: C++ has no opinion
	 *  about what a highlight looks like, only about when one is due. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory Slot", meta = (DisplayName = "Set Drop Target Highlight"))
	void BP_SetDropTargetHighlight(bool bHighlighted);

	/** What flies under the cursor while dragging. Left unset, the square makes a copy of itself,
	 *  which is right almost always: the thing being dragged looks exactly like the thing that was
	 *  picked up. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory Slot|Drag")
	TSubclassOf<UInventorySlotWidget> DragVisualClass;

	/** The single hook a WBP has to implement.
	 *
	 *  FillRatio is Count/StackMax, already clamped, so the Blueprint can drive the bottom-up fill
	 *  without knowing that currency stacks to 100 and an ability upgrade does not stack at all.
	 *  Anything that does not stack arrives with a ratio of 1, which is what makes a full cell and
	 *  a single indivisible item look the same on purpose. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory Slot", meta = (DisplayName = "Set Cell"))
	void BP_SetCell(EInventoryCellVisual InVisual, EInventorySlotKind InKind, UTexture2D* InIcon,
		int32 InCount, int32 InStackMax, float FillRatio, bool bShowCount);

	UPROPERTY(BlueprintReadOnly, Category = "Inventory Slot")
	EInventoryCellVisual Visual = EInventoryCellVisual::Empty;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory Slot")
	EInventorySlotKind Kind = EInventorySlotKind::Empty;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory Slot")
	int32 Count = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory Slot")
	int32 StackMax = 0;

	// ==================== Optional parts the WBP may supply ====================
	//
	// All four are optional, so a Blueprint that wants to draw the square entirely by itself simply
	// leaves them out and works off BP_SetCell. A Blueprint that names them gets the whole square
	// driven from here with no graph at all, which is the normal case: the look of a cell is data,
	// not behaviour, and there is nothing for a graph to decide.

	/** Plate behind everything. Which brush it wears is what says Locked or ghost. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> BackgroundImage;

	/** The bottom-up fill. A progress bar rather than a resized image on purpose: BarFillType
	 *  BottomToTop is exactly this, and it costs one SetPercent instead of brush arithmetic that
	 *  would have to know how big the square is. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> FillBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> IconImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CountText;

	// ==================== Look, as data ====================

	/** Colour per kind: gold for currency, steel for ammo, violet for an ability upgrade, volt for
	 *  an attachment. Data rather than a switch in code so the palette is tuned in the WBP without
	 *  a rebuild. A kind that is missing falls back to DefaultColor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory Slot|Look")
	TMap<EInventorySlotKind, FLinearColor> KindColors;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory Slot|Look")
	FLinearColor DefaultColor = FLinearColor(0.60f, 0.66f, 0.69f, 1.0f);

	/** Plate for an ordinary square, empty or filled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory Slot|Look")
	FSlateBrush NormalBrush;

	/** Plate for a square the meta has not bought: dashed and hatched. C++ cannot draw a dash, it
	 *  only picks which of these three brushes goes on. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory Slot|Look")
	FSlateBrush LockedBrush;

	/** Plate for the ghost: an attachment mounted on a weapon and still holding this cell. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory Slot|Look")
	FSlateBrush HeldBrush;

	/** How faint the fill is against the icon of the same colour. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory Slot|Look", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FillOpacity = 0.16f;

	/** How faint the ghost icon is. Low enough to read as "spoken for" rather than "here". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory Slot|Look", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GhostOpacity = 0.30f;

private:

	/** Push Visual/Kind/Count into whichever of the four optional parts the WBP supplied. */
	void ApplyLook(UTexture2D* InIcon, float FillRatio, bool bShowCount);

	FLinearColor ColorForKind() const;

	/** Which cell of the grid this square draws, or INDEX_NONE for a square that is not a grid cell
	 *  at all. Also the drag gate: see SetGridIndex. */
	int32 GridIndex = INDEX_NONE;

	/** The weapon this square is an attachment slot on, or null for a grid cell. The other half of
	 *  the same gate GridIndex is: exactly one of the two is ever set. */
	UPROPERTY(Transient)
	TObjectPtr<AShooterWeapon> AttachmentWeapon;

	/** What is fitted in this attachment slot right now. Null means the slot is empty, and an empty
	 *  slot has nothing to drag off. */
	UPROPERTY(Transient)
	TObjectPtr<UWeaponAttachmentDefinition> MountedAttachment;

	/** The picture currently on the square. Kept only so the drag visual can be an exact copy of
	 *  what was picked up rather than an empty square of the right size. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CurrentIcon;

	/** The definition asset in this cell, when it has one. Kept so a drag can say what is being
	 *  carried and a weapon's squares can refuse an obvious mismatch without a round trip. */
	UPROPERTY(Transient)
	TObjectPtr<UObject> CurrentPayload;

};
