// InventorySlotWidget.cpp

#include "Variant_Shooter/UI/InventorySlotWidget.h"
#include "Variant_Shooter/UI/InventoryDragDropOperation.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

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
	const bool bShowCount =
		(InKind == EInventorySlotKind::Currency || InKind == EInventorySlotKind::Ammo) && InCount > 0;

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

	if (const FLinearColor* Found = KindColors.Find(Kind))
	{
		return *Found;
	}
	return DefaultColor;
}

void UInventorySlotWidget::ApplyLook(UTexture2D* InIcon, float FillRatio, bool bShowCount)
{
	const bool bLocked = (Visual == EInventoryCellVisual::Locked);
	const bool bGhost = (Visual == EInventoryCellVisual::HeldByAttachment);

	FLinearColor KindColor = ColorForKind();
	if (bGhost)
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
			CountText->SetText(FText::AsNumber(Count));
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
	const int32 EffectiveStackMax = InSlot.IsEmpty() ? 0 : FMath::Max(1, InSlot.StackMax);
	const int32 EffectiveCount = InSlot.IsEmpty() ? 0 : FMath::Max(1, InSlot.Count);

	// Recorded before SetCell, because SetCell is what repaints and ColorForKind reads this.

	// What is actually in the cell, so a drag off this square can say what it is carrying.
	CurrentPayload = InSlot.Payload;

	SetCell(NewVisual, InSlot.Kind, InIcon, EffectiveCount, EffectiveStackMax);
}

void UInventorySlotWidget::SetAttachmentTarget(AShooterWeapon* InWeapon, UWeaponAttachmentDefinition* Mounted)
{
	AttachmentWeapon = InWeapon;
	MountedAttachment = Mounted;
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
	const bool bHasSomethingToDrag = (GridIndex != INDEX_NONE && Visual == EInventoryCellVisual::Filled)
		|| (AttachmentWeapon && MountedAttachment);

	if (bHasSomethingToDrag && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Handled plus DetectDrag rather than starting one here: a click that never moves is not a
		// drag, and Slate is what knows the difference.
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UInventorySlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
	UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);

	const bool bFromGrid = GridIndex != INDEX_NONE && Visual == EInventoryCellVisual::Filled;
	const bool bFromWeapon = AttachmentWeapon != nullptr && MountedAttachment != nullptr;

	if (!bFromGrid && !bFromWeapon)
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
		Ghost->SetCell(Visual, Kind, CurrentIcon, Count, StackMax);
		Operation->DefaultDragVisual = Ghost;
	}

	OutOperation = Operation;
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

		// Only from the bag. Moving a part straight from one gun to the other would be a removal
		// and a mount in one gesture, and the removal can be refused (a full bag), which would
		// leave the player watching an attachment go nowhere with no way to see why.
		//
		// And only onto a gun it fits. The server checks the same thing and has the last word; this
		// just saves a round trip that was always going to be refused.
		if (Dragged->SourceIndex != INDEX_NONE && Dragged->DraggedAttachment
			&& Dragged->DraggedAttachment->FitsWeapon(AttachmentWeapon->GetClass()))
		{
			OnAttachmentInstall.ExecuteIfBound(Dragged->SourceIndex, AttachmentWeapon);
		}
		return true;
	}

	// --- This square is a cell of the bag ---
	if (GridIndex == INDEX_NONE)
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}

	// A cell the meta has not bought is a wall, not a destination.
	if (Visual == EInventoryCellVisual::Locked)
	{
		return true;
	}

	// Dragged off a gun: this cell is where it should land.
	if (Dragged->SourceWeapon)
	{
		OnAttachmentRemove.ExecuteIfBound(Dragged->SourceWeapon, Dragged->SourceAttachmentType, GridIndex);
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

	const bool bIsDropTarget = (GridIndex != INDEX_NONE || AttachmentWeapon != nullptr)
		&& Visual != EInventoryCellVisual::Locked;

	const UInventoryDragDropOperation* Dragged = Cast<UInventoryDragDropOperation>(InOperation);

	// A gun's square lights up only for an attachment that fits that gun, so a 4x scope dragged over
	// the pistol stays dark instead of promising a mount the server will refuse.
	const bool bFits = !AttachmentWeapon
		|| (Dragged && Dragged->DraggedAttachment
			&& Dragged->DraggedAttachment->FitsWeapon(AttachmentWeapon->GetClass()));

	if (bIsDropTarget && Dragged && bFits)
	{
		BP_SetDropTargetHighlight(true);
	}
}

void UInventorySlotWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);
	BP_SetDropTargetHighlight(false);
}
