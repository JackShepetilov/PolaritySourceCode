// InventorySlotWidget.cpp

#include "Variant_Shooter/UI/InventorySlotWidget.h"
#include "Variant_Shooter/UI/InventoryDragDropOperation.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Upgrades/DispenserUpgradePool.h"
#include "Upgrades/UpgradeDefinition.h"
#include "Variant_Shooter/Inventory/InventoryIconSettings.h"

#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Input/Reply.h"

void UInventorySlotWidget::SetCell(EInventoryCellVisual InVisual, EInventorySlotKind InKind,
	UTexture2D* InIcon, int32 InCount, int32 InStackMax)
{
	Visual = InVisual;
	Kind = InKind;
	Count = InCount;
	StackMax = InStackMax;
	// Kept so a drag can make an exact copy of this square without being handed the icon again.
	CurrentIcon = InIcon;

	// A stack of one is full, and so is anything that does not stack. Falling back to 1 rather than
	// 0 is what keeps an ability upgrade from drawing as an empty cell with an icon floating in it.
	const float FillRatio = (InStackMax > 0)
		? FMath::Clamp(static_cast<float>(InCount) / static_cast<float>(InStackMax), 0.0f, 1.0f)
		: 0.0f;

	// The number is only meaningful where a cell can be partly full. Showing "1" on an upgrade
	// would be noise on a HUD read out of the corner of the eye.
	// An upgrade shows its level instead: two copies of the same upgrade at two levels are two
	// different decisions.
	const bool bShowCount =
		(InKind == EInventorySlotKind::Currency || InKind == EInventorySlotKind::Ammo
			|| InKind == EInventorySlotKind::AbilityUpgrade) && InCount > 0;

	// Whatever parts the Blueprint supplied get driven from here first, then the Blueprint gets its
	// turn. That order matters: BP_SetCell is the override hook, so anything it sets has to be able
	// to win over the default treatment.
	ApplyLook(InIcon, FillRatio, bShowCount);

	BP_SetCell(Visual, Kind, InIcon, Count, StackMax, FillRatio, bShowCount);
}

FLinearColor UInventorySlotWidget::ColorForKind() const
{
	// Ammo used to take its colour from the weapon it fed, so two magazines could be told apart.
	// With one pool [author, 2026-09-01] there is only one kind of rounds, and it wears the colour
	// of its kind like everything else.

	// A thing with a rarity wears the rarity colour: the icon is a white silhouette for this.
	if (bUseRarityTint && (Kind == EInventorySlotKind::Attachment || Kind == EInventorySlotKind::AbilityUpgrade))
	{
		return UInventoryIconSettings::GetRarityColor(TintRarity);
	}

	if (const FLinearColor* Found = KindColors.Find(Kind))
	{
		return *Found;
	}
	return DefaultColor;
}

void UInventorySlotWidget::SetRarityTint(bool bInUse, EUpgradeRarity InRarity)
{
	bUseRarityTint = bInUse;
	TintRarity = InRarity;
}

void UInventorySlotWidget::SetFromWeaponSlot(AShooterWeapon* Weapon, EWeaponAttachmentType Type, int32 FreeSlots)
{
	UWeaponAttachmentDefinition* Here = Weapon ? Weapon->GetAttachmentOfType(Type) : nullptr;
	const int32 MountedCount = Weapon ? Weapon->GetInstalledAttachmentCount() : 0;

	// Filled when something is in it; free while the gun has free mounts left; Paid otherwise,
	// which is usable but takes a cell out of the bag. The first FreeSlots mounts are the free
	// ones, the same subtraction the inventory does.
	EInventoryCellVisual CellVisual = EInventoryCellVisual::Paid;
	if (Here)
	{
		CellVisual = EInventoryCellVisual::Filled;
	}
	else if (MountedCount < FreeSlots)
	{
		CellVisual = EInventoryCellVisual::Empty;
	}

	SetRarityTint(Here != nullptr, Here ? Here->Rarity : EUpgradeRarity::Common);
	SetCell(CellVisual, Here ? EInventorySlotKind::Attachment : EInventorySlotKind::Empty,
		Here ? Here->GetDisplayIcon() : UInventoryIconSettings::GetTypeIcon(Type), Here ? 1 : 0, Here ? 1 : 0);
}

void UInventorySlotWidget::ApplyLook(UTexture2D* InIcon, float FillRatio, bool bShowCount)
{
	const bool bLocked = (Visual == EInventoryCellVisual::Locked);
	const bool bGhost = (Visual == EInventoryCellVisual::HeldByAttachment);
	// An empty weapon slot shows its type's icon as a hint of what goes there, as faint as a ghost.
	const bool bHint = (Visual == EInventoryCellVisual::Empty || Visual == EInventoryCellVisual::Paid);

	FLinearColor KindColor = ColorForKind();
	if (bGhost || bHint)
	{
		KindColor.A *= GhostOpacity;
	}

	if (BackgroundImage)
	{
		// The plate is the only thing that says which state the square is in, which is why it is
		// three authored brushes rather than a tint: a dash and a hatch are not a colour.
		BackgroundImage->SetBrush(bLocked ? LockedBrush : (bGhost ? HeldBrush : NormalBrush));
	}

	if (FillBar)
	{
		// A ghost cell is held, not filled: showing a bar there would read as contents.
		const bool bShowFill = !bLocked && !bGhost && FillRatio > 0.0f;
		FillBar->SetVisibility(bShowFill ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

		if (bShowFill)
		{
			FillBar->SetPercent(FillRatio);

			FLinearColor FillColor = KindColor;
			FillColor.A *= FillOpacity;
			FillBar->SetFillColorAndOpacity(FillColor);
		}
	}

	if (IconImage)
	{
		const bool bShowIcon = (InIcon != nullptr) && !bLocked;
		IconImage->SetVisibility(bShowIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

		if (bShowIcon)
		{
			IconImage->SetBrushFromTexture(InIcon, false);
			IconImage->SetColorAndOpacity(KindColor);
		}
	}

	if (CountText)
	{
		CountText->SetVisibility(bShowCount ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

		if (bShowCount)
		{
			CountText->SetText(Kind == EInventorySlotKind::AbilityUpgrade
				? FText::Format(NSLOCTEXT("Inventory", "UpgradeLevel", "Lv {0}"), FText::AsNumber(Count))
				: FText::AsNumber(Count));
			CountText->SetColorAndOpacity(FSlateColor(KindColor));
		}
	}
}

void UInventorySlotWidget::SetFromSlot(const FInventorySlot& InSlot, UTexture2D* InIcon)
{
	EInventoryCellVisual NewVisual = EInventoryCellVisual::Filled;

	if (InSlot.IsEmpty())
	{
		NewVisual = EInventoryCellVisual::Empty;
	}
	else if (InSlot.Kind == EInventorySlotKind::Attachment && InSlot.bInstalled)
	{
		NewVisual = EInventoryCellVisual::HeldByAttachment;
	}

	// An empty cell keeps a fill of zero by carrying no stack at all; a filled one that does not
	// stack reports 1/1 so the Blueprint sees a full square.
	int32 EffectiveStackMax = InSlot.IsEmpty() ? 0 : FMath::Max(1, InSlot.StackMax);
	int32 EffectiveCount = InSlot.IsEmpty() ? 0 : FMath::Max(1, InSlot.Count);

	// An upgrade's number is its level; a full square either way.
	if (InSlot.Kind == EInventorySlotKind::AbilityUpgrade)
	{
		EffectiveCount = EffectiveStackMax = FMath::Max(1, InSlot.Level);
	}

	// Rarity tint: an attachment's own rarity, an upgrade's at the level it is carried at.
	if (const UWeaponAttachmentDefinition* Attachment = Cast<UWeaponAttachmentDefinition>(InSlot.Payload))
	{
		SetRarityTint(true, Attachment->Rarity);
	}
	else if (const UUpgradeDefinition* Upgrade = Cast<UUpgradeDefinition>(InSlot.Payload))
	{
		SetRarityTint(true, Upgrade->GetLevelRarity(FMath::Max(1, InSlot.Level)));
	}
	else
	{
		SetRarityTint(false);
	}

	// Recorded before SetCell, because SetCell is what repaints and ColorForKind reads this.

	// What is actually in the cell, so a drag off this square can say what it is carrying.
	CurrentPayload = InSlot.Payload;

	SetCell(NewVisual, InSlot.Kind, InIcon, EffectiveCount, EffectiveStackMax);
}

void UInventorySlotWidget::SetAttachmentTarget(AShooterWeapon* InWeapon, UWeaponAttachmentDefinition* Mounted,
	EWeaponAttachmentType InSlotType)
{
	AttachmentWeapon = InWeapon;
	MountedAttachment = Mounted;
	AttachmentSlotType = InSlotType;
}

bool UInventorySlotWidget::AcceptsAttachment(const UWeaponAttachmentDefinition* Attachment) const
{
	return Attachment && AttachmentWeapon && Attachment->Type == AttachmentSlotType
		&& Attachment->FitsWeapon(AttachmentWeapon->GetClass());
}

void UInventorySlotWidget::SetUpgradeSlotTarget(int32 InSlotIndex, const UDispenserUpgradePool* InLayout, UUpgradeDefinition* Equipped)
{
	UpgradeSlotIndex = InSlotIndex;
	UpgradeSlotLayout = InLayout;
	EquippedUpgrade = Equipped;
}

bool UInventorySlotWidget::AcceptsUpgrade(const UUpgradeDefinition* Upgrade) const
{
	const UDispenserUpgradePool* Layout = UpgradeSlotLayout.Get();
	return Upgrade && Layout && UpgradeSlotIndex != INDEX_NONE && Layout->FindSlotOf(Upgrade) == UpgradeSlotIndex;
}

// ==================== Drag and drop ====================
//
// A square has exactly one identity and it is what decides everything here: either it is a cell of
// the bag (GridIndex) or it is an attachment slot on a gun (AttachmentWeapon). Never both, and a
// square that is neither takes no part in dragging at all -- which is the whole gate, with no
// separate "is this draggable" flag to fall out of step with reality.
//
// Fitting an attachment and taking one off are the SAME gesture in opposite directions, so they
// share one operation object rather than two that every drop target would have to tell apart.
//
// Nothing here writes to the inventory. The square reports what landed on what, and the screen
// decides what that means, because the grid is the server's and a widget has no business knowing
// about authority.

FReply UInventorySlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const bool bGridItem = GridIndex != INDEX_NONE && Visual == EInventoryCellVisual::Filled;
	const bool bMounted = AttachmentWeapon && MountedAttachment;
	const bool bHasSomethingToDrag = bGridItem || bMounted || (UpgradeSlotIndex != INDEX_NONE && EquippedUpgrade);

	// Right click, Apex-style: a bag item goes on the floor, a fitted part goes back to the bag.
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton && (bGridItem || bMounted))
	{
		if (bGridItem)
		{
			OnCellClicked.ExecuteIfBound(GridIndex, true);
		}
		else
		{
			OnAttachmentClicked.ExecuteIfBound(AttachmentWeapon, MountedAttachment->Type, true);
		}
		return FReply::Handled();
	}

	if (bHasSomethingToDrag && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Handled plus DetectDrag rather than starting one here: a click that never moves is not a
		// drag, and Slate is what knows the difference. A release before it becomes one is a click.
		bLeftPressPending = true;
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UInventorySlotWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bLeftPressPending && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bLeftPressPending = false;
		if (GridIndex != INDEX_NONE && Visual == EInventoryCellVisual::Filled)
		{
			OnCellClicked.ExecuteIfBound(GridIndex, false);
		}
		else if (AttachmentWeapon && MountedAttachment)
		{
			OnAttachmentClicked.ExecuteIfBound(AttachmentWeapon, MountedAttachment->Type, false);
		}
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UInventorySlotWidget::SetCompatibleHint(bool bInHint)
{
	bCompatibleHint = bInHint;
	BP_SetDropTargetHighlight(bInHint);
}

bool UInventorySlotWidget::WouldAcceptDrag(const UDragDropOperation* Operation) const
{
	const UInventoryDragDropOperation* Dragged = Cast<UInventoryDragDropOperation>(Operation);
	if (!Dragged || !AttachmentWeapon || Visual == EInventoryCellVisual::Locked || Dragged->DraggedWeapon)
	{
		return false;
	}
	// A part from the bag, or from the other gun; never back onto the slot it came from.
	const bool bFromBag = Dragged->SourceIndex != INDEX_NONE;
	const bool bFromOtherGun = Dragged->SourceWeapon && Dragged->SourceWeapon != AttachmentWeapon;
	return (bFromBag || bFromOtherGun) && AcceptsAttachment(Dragged->DraggedAttachment);
}

void UInventorySlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
	UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);
	bLeftPressPending = false;

	const bool bFromGrid = GridIndex != INDEX_NONE && Visual == EInventoryCellVisual::Filled;
	const bool bFromWeapon = AttachmentWeapon != nullptr && MountedAttachment != nullptr;
	const bool bFromUpgradeSlot = UpgradeSlotIndex != INDEX_NONE && EquippedUpgrade != nullptr;

	if (!bFromGrid && !bFromWeapon && !bFromUpgradeSlot)
	{
		return;
	}

	UInventoryDragDropOperation* Operation = NewObject<UInventoryDragDropOperation>(GetTransientPackage());
	Operation->Pivot = EDragPivot::CenterCenter;

	if (bFromGrid)
	{
		Operation->SourceIndex = GridIndex;
		// A hint for the cursor only, so a weapon's squares can refuse an obvious mismatch without
		// asking the server. The server re-reads the cell and never trusts this.
		Operation->DraggedAttachment = Cast<UWeaponAttachmentDefinition>(CurrentPayload);
		Operation->DraggedUpgrade = Cast<UUpgradeDefinition>(CurrentPayload);
	}
	else if (bFromUpgradeSlot)
	{
		Operation->SourceUpgradeSlot = UpgradeSlotIndex;
		Operation->DraggedUpgrade = EquippedUpgrade;
	}
	else
	{
		Operation->SourceWeapon = AttachmentWeapon;
		Operation->SourceAttachmentType = MountedAttachment->Type;
		Operation->DraggedAttachment = MountedAttachment;
	}

	// A copy of this square, not this square itself: handing the live widget to the drag layer
	// would tear the cell out of the grid and leave a hole where the player is still looking.
	UClass* VisualClass = DragVisualClass ? DragVisualClass.Get() : GetClass();
	if (UInventorySlotWidget* Ghost = CreateWidget<UInventorySlotWidget>(this, VisualClass))
	{
		Ghost->SetRarityTint(bUseRarityTint, TintRarity);
		Ghost->SetCell(Visual, Kind, CurrentIcon, Count, StackMax);
		Operation->DefaultDragVisual = Ghost;
	}

	OutOperation = Operation;
	OnDragStarted.ExecuteIfBound(Operation);
}

bool UInventorySlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	BP_SetDropTargetHighlight(false);

	const UInventoryDragDropOperation* Dragged = Cast<UInventoryDragDropOperation>(InOperation);
	if (!Dragged)
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}

	// --- This square is a slot on a gun ---
	if (AttachmentWeapon)
	{
		// Swallowed either way: the drop was aimed here, and letting it fall through would let the
		// screen underneath read the same mouse release as "thrown on the floor".
		if (Visual == EInventoryCellVisual::Locked)
		{
			return true;
		}

		// Apex-style [author, 2026-09-29]: from the bag it fits, and a part already in this slot
		// swaps back into the bag; from the other gun it moves across, and what was here goes back to
		// that gun. Only onto a gun it fits: the server checks the same and has the last word, this
		// just saves a round trip that was always going to be refused.
		if (WouldAcceptDrag(Dragged))
		{
			if (Dragged->SourceIndex != INDEX_NONE)
			{
				OnAttachmentInstall.ExecuteIfBound(Dragged->SourceIndex, AttachmentWeapon);
			}
			else if (Dragged->SourceWeapon)
			{
				OnAttachmentMove.ExecuteIfBound(Dragged->SourceWeapon, Dragged->SourceAttachmentType, AttachmentWeapon);
			}
		}
		return true;
	}

	// --- This square is an action slot ---
	if (UpgradeSlotIndex != INDEX_NONE)
	{
		// Only from the bag, and only an upgrade of this slot. Swallowed either way, for the same
		// reason a weapon's square swallows: this drop was aimed here, not at the floor.
		if (Dragged->SourceIndex != INDEX_NONE && AcceptsUpgrade(Dragged->DraggedUpgrade))
		{
			OnUpgradeEquip.ExecuteIfBound(Dragged->SourceIndex, UpgradeSlotIndex);
		}
		return true;
	}

	// --- This square is a cell of the bag ---
	if (GridIndex == INDEX_NONE)
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}

	// A cell the meta has not bought is a wall, not a destination. A whole gun has no business in a
	// cell either: swallowed, so it is not read as thrown on the floor.
	if (Visual == EInventoryCellVisual::Locked || Dragged->DraggedWeapon)
	{
		return true;
	}

	// Dragged off a gun: this cell is where it should land.
	if (Dragged->SourceWeapon)
	{
		OnAttachmentRemove.ExecuteIfBound(Dragged->SourceWeapon, Dragged->SourceAttachmentType, GridIndex);
		return true;
	}

	// Dragged out of an action slot: this cell is where it should land.
	if (Dragged->SourceUpgradeSlot != INDEX_NONE)
	{
		OnUpgradeUnequip.ExecuteIfBound(Dragged->SourceUpgradeSlot, GridIndex);
		return true;
	}

	if (Dragged->SourceIndex == INDEX_NONE || Dragged->SourceIndex == GridIndex)
	{
		return true;
	}

	OnCellDropped.ExecuteIfBound(Dragged->SourceIndex, GridIndex);
	return true;
}

void UInventorySlotWidget::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	Super::NativeOnDragEnter(InGeometry, InDragDropEvent, InOperation);

	const bool bIsDropTarget = (GridIndex != INDEX_NONE || AttachmentWeapon != nullptr || UpgradeSlotIndex != INDEX_NONE)
		&& Visual != EInventoryCellVisual::Locked;

	const UInventoryDragDropOperation* Dragged = Cast<UInventoryDragDropOperation>(InOperation);

	// A gun's square lights up only for an attachment of its own type that fits the gun, so a 4x
	// scope dragged over the pistol stays dark instead of promising a mount the server will refuse.
	bool bFits = AttachmentWeapon ? WouldAcceptDrag(Dragged) : (Dragged && !Dragged->DraggedWeapon);

	// An action slot lights up only for an upgrade of its own, dragged out of the bag.
	if (UpgradeSlotIndex != INDEX_NONE)
	{
		bFits = Dragged && Dragged->SourceIndex != INDEX_NONE && AcceptsUpgrade(Dragged->DraggedUpgrade);
	}

	if (bIsDropTarget && Dragged && bFits)
	{
		BP_SetDropTargetHighlight(true);
	}
}

void UInventorySlotWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);
	// Back to the whole-drag hint rather than dark: the slot still would take what is being carried.
	BP_SetDropTargetHighlight(bCompatibleHint);
}
