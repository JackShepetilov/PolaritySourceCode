// InventoryScreenWidget.cpp

#include "Variant_Shooter/UI/InventoryScreenWidget.h"

#include "Components/PanelWidget.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "GameFramework/PlayerController.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/UI/InventoryDragDropOperation.h"
#include "Variant_Shooter/UI/InventorySlotWidget.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

namespace
{
	/** The grid is always two rows tall and grows sideways: 2x5 at a ceiling of ten. Two rows is
	 *  the shape Apex uses, and it keeps every cell within one glance of the cursor. */
	constexpr int32 GridRowCount = 2;
}

void UInventoryScreenWidget::InitializeFor(AShooterCharacter* InCharacter)
{
	// Unbind first so a respawn can call this again without stacking delegates. Shutdown also
	// closes, which is what keeps input from being stranded in cursor mode across a death.
	Shutdown();

	if (!InCharacter)
	{
		return;
	}

	BoundCharacter = InCharacter;

	if (UInventoryComponent* Inventory = InCharacter->GetInventoryComponent())
	{
		Inventory->OnInventoryChanged.AddDynamic(this, &UInventoryScreenWidget::HandleInventoryChanged);
	}

	InCharacter->OnWeaponInventoryChanged.AddDynamic(this, &UInventoryScreenWidget::HandleWeaponInventoryChanged);
	InCharacter->OnBulletCountUpdated.AddDynamic(this, &UInventoryScreenWidget::HandleBulletCount);

	// Built while hidden. The grid is at most eight squares, so paying for it up front costs
	// nothing and the first press of the key has no hitch.
	SetVisibility(ESlateVisibility::Collapsed);
	Rebuild();
}

void UInventoryScreenWidget::Shutdown()
{
	Close();

	if (AShooterCharacter* Character = BoundCharacter.Get())
	{
		if (UInventoryComponent* Inventory = Character->GetInventoryComponent())
		{
			Inventory->OnInventoryChanged.RemoveDynamic(this, &UInventoryScreenWidget::HandleInventoryChanged);
		}

		Character->OnWeaponInventoryChanged.RemoveDynamic(this, &UInventoryScreenWidget::HandleWeaponInventoryChanged);
		Character->OnBulletCountUpdated.RemoveDynamic(this, &UInventoryScreenWidget::HandleBulletCount);
	}

	BoundCharacter = nullptr;
}

void UInventoryScreenWidget::NativeDestruct()
{
	Shutdown();
	Super::NativeDestruct();
}

// ==================== Open and close ====================

void UInventoryScreenWidget::Open()
{
	if (bIsOpen)
	{
		return;
	}

	bIsOpen = true;

	// Contents may have moved while the screen was hidden: the delegates rebuild either way, but a
	// screen opened for the first time after a pickup would otherwise show the state it was built
	// with.
	Rebuild();
	SetVisibility(ESlateVisibility::Visible);

	if (APlayerController* PC = GetOwningPlayer())
	{
		// GameAndUI rather than UIOnly on purpose: movement keys must keep reaching the pawn.
		// Standing still to read the inventory is what makes an inventory a menu, and this one is
		// opened in the middle of a fight.
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
	}

	// No SetGamePaused here, and there must never be one: this is a co-op game, and pausing for one
	// player freezes the other three. See the header.
	BP_OnOpened();
}

void UInventoryScreenWidget::Close()
{
	if (!bIsOpen)
	{
		return;
	}

	bIsOpen = false;

	SetVisibility(ESlateVisibility::Collapsed);

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}

	BP_OnClosed();
}

void UInventoryScreenWidget::Toggle()
{
	if (bIsOpen)
	{
		Close();
	}
	else
	{
		Open();
	}
}

// ==================== Drag and drop ====================

void UInventoryScreenWidget::HandleCellDropped(int32 FromIndex, int32 ToIndex)
{
	RequestMove(FromIndex, ToIndex);
}

void UInventoryScreenWidget::RequestMove(int32 FromIndex, int32 ToIndex)
{
	AShooterCharacter* Character = BoundCharacter.Get();
	UInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	if (!Inventory)
	{
		return;
	}

	// The array this screen is drawing is a replicated copy. Writing to it here would look right
	// for one frame and then be overwritten by the server's next update, so the cursor asks
	// instead. On a listen server the host is the authority and the call goes straight through.
	if (Character->HasAuthority())
	{
		Inventory->MoveSlot(FromIndex, ToIndex);
	}
	else
	{
		Inventory->Server_MoveSlot(FromIndex, ToIndex);
	}
}

void UInventoryScreenWidget::RequestDropToWorld(int32 SlotIndex)
{
	AShooterCharacter* Character = BoundCharacter.Get();
	UInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	if (!Inventory)
	{
		return;
	}

	if (Character->HasAuthority())
	{
		Inventory->DropSlotToWorld(SlotIndex);
	}
	else
	{
		Inventory->Server_DropSlotToWorld(SlotIndex);
	}
}

bool UInventoryScreenWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	const UInventoryDragDropOperation* Dragged = Cast<UInventoryDragDropOperation>(InOperation);
	if (!Dragged)
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}

	// Dragged off a weapon and let go anywhere that is not a cell. That is "take it off", not
	// "throw it away": the part is not in the bag yet, so there is nothing to throw, and the
	// component finds it the first cell that will have it. A full bag refuses and says so.
	if (Dragged->SourceWeapon)
	{
		HandleAttachmentRemove(Dragged->SourceWeapon, Dragged->SourceAttachmentType, INDEX_NONE);
		return true;
	}

	if (Dragged->SourceIndex == INDEX_NONE || !bDropOutsideGridDropsToWorld)
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}

	// Nothing caught this drop on the way here, which means it landed on the screen and not on a
	// cell. That gesture is "throw it away": the item goes into the world where the player can pick
	// it back up, which is the only reason it is safe to hang on a mis-click.
	RequestDropToWorld(Dragged->SourceIndex);
	return true;
}

// ==================== Rebuild ====================

void UInventoryScreenWidget::HandleInventoryChanged()
{
	Rebuild();
}

void UInventoryScreenWidget::HandleWeaponInventoryChanged()
{
	Rebuild();
}

void UInventoryScreenWidget::HandleBulletCount(int32 MagazineSize, int32 Bullets)
{
	// Only worth the work while the player is looking at it. The screen is rebuilt on Open anyway,
	// and this fires on every shot: rebuilding a hidden grid once per bullet buys nothing.
	if (bIsOpen)
	{
		Rebuild();
	}
}

void UInventoryScreenWidget::Rebuild()
{
	AShooterCharacter* Character = BoundCharacter.Get();
	if (!Character)
	{
		return;
	}

	const UInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (!Inventory)
	{
		return;
	}

	RebuildGrid(*Inventory);
	RebuildWeaponPanels(*Inventory);

	// Attachment slots are per weapon and the count is the same for both, so both columns get the
	// same numbers. What is mounted differs, so each column is given its own weapon.
	const int32 FreeSlots = Inventory->GetFreeAttachmentSlots();
	const int32 MaxSlots = Inventory->GetMaxFreeAttachmentSlots();
	const TArray<AShooterWeapon*>& Weapons = Character->GetOwnedWeapons();

	RebuildAttachmentSlots(FirstWeaponAttachments,
		Weapons.IsValidIndex(0) ? Weapons[0] : nullptr, FreeSlots, MaxSlots);
	RebuildAttachmentSlots(SecondWeaponAttachments,
		Weapons.IsValidIndex(1) ? Weapons[1] : nullptr, FreeSlots, MaxSlots);
}

void UInventoryScreenWidget::RebuildWeaponPanels(const UInventoryComponent& Inventory)
{
	AShooterCharacter* Character = BoundCharacter.Get();
	if (!Character)
	{
		return;
	}

	// Same rule as the corner: a panel is a SLOT. Panel 0 is the first weapon owned and panel 1 the
	// second, and neither moves when the player switches - the labels on the reference screen read
	// "1 WEAPON" and "2 WEAPON", which only means anything if the slots stay put. bIsEquipped is
	// what says which one is in hand.
	const TArray<AShooterWeapon*>& Weapons = Character->GetOwnedWeapons();
	const AShooterWeapon* Equipped = Character->GetCurrentWeapon();

	AShooterWeapon* Panels[2] = {
		Weapons.IsValidIndex(0) ? Weapons[0] : nullptr,
		Weapons.IsValidIndex(1) ? Weapons[1] : nullptr
	};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		AShooterWeapon* Weapon = Panels[Index];

		// Same rule as the corner: a weapon that owns no cells has no reserve to show, whether it
		// never reloads or reloads from an endless one.
		const bool bInfinite = Weapon && !Weapon->HasFiniteReserve();
		// Unloaded rounds only, same as the corner: the cells include what is in the gun, the
		// energy reserve does not.
		int32 Reserve = 0;
		if (Weapon && Weapon->UsesEnergyReserve())
		{
			Reserve = Weapon->GetEnergyReserve();
		}
		else if (Weapon && !bInfinite)
		{
			Reserve = FMath::Max(0, Inventory.GetAmmo() - Weapon->GetBulletCount());
		}

		BP_SetWeaponPanel(Index, Weapon,
			Weapon ? Weapon->GetBulletCount() : 0,
			Weapon ? Weapon->GetMagazineSize() : 0,
			Reserve, bInfinite,
			Weapon && Weapon == Equipped);
	}
}

void UInventoryScreenWidget::RebuildGrid(const UInventoryComponent& Inventory)
{
	if (!GridContainer || !GridCellClass)
	{
		return;
	}

	// Every cell up to the ceiling is drawn, and the ones the meta has not bought yet are
	// padlocked, the way Apex shows the rows a bigger backpack would open. A cell the player can
	// see but not use is the only advertisement the hub needs, and it also stops the grid from
	// changing shape under the cursor every time capacity is bought.
	const TArray<FInventorySlot>& Slots = Inventory.GetSlots();
	const int32 OwnedCount = Slots.Num();
	const int32 CellCount = FMath::Max(OwnedCount, Inventory.GetMaxSlotCount());
	const int32 ColumnCount = FMath::Max(1, FMath::DivideAndRoundUp(CellCount, GridRowCount));

	// Capacity changing moves every square: at four cells the grid is two columns wide, at six it
	// is three, so square 2 belongs on the bottom row in one and the top row in the other. Reusing
	// squares across that would leave them in their old cells, so the whole grid is rebuilt when
	// the count changes and only reused when it did not.
	if (GridSquares.Num() != CellCount)
	{
		GridContainer->ClearChildren();
		GridSquares.Reset();
		GridSquares.SetNum(CellCount);

		for (int32 Index = 0; Index < CellCount; ++Index)
		{
			UInventorySlotWidget* Square = CreateWidget<UInventorySlotWidget>(this, GridCellClass);
			if (!Square)
			{
				continue;
			}

			if (UUniformGridSlot* GridSlot = GridContainer->AddChildToUniformGrid(
				Square, Index / ColumnCount, Index % ColumnCount))
			{
				GridSlot->SetHorizontalAlignment(HAlign_Left);
				GridSlot->SetVerticalAlignment(VAlign_Top);
			}

			// The square has to be hit-testable or the cursor passes straight through it and both
			// halves of dragging stop working. Forced here rather than trusted to the WBP: a cell
			// left on SelfHitTestInvisible looks completely correct and does nothing at all.
			Square->SetVisibility(ESlateVisibility::Visible);

			// Which cell it IS. This is also what makes it draggable; the attachment squares below
			// are deliberately never told, so they neither start a drag nor accept one.
			Square->SetGridIndex(Index);
			Square->OnCellDropped.BindUObject(this, &UInventoryScreenWidget::HandleCellDropped);

			GridSquares[Index] = Square;
		}
	}

	// Contents only. On the common path - a pickup landing in an existing grid - the squares are
	// the same objects as last frame, so any Blueprint animation on the ones that did not change
	// keeps running instead of restarting.
	for (int32 Index = 0; Index < CellCount; ++Index)
	{
		UInventorySlotWidget* Square = GridSquares.IsValidIndex(Index) ? GridSquares[Index].Get() : nullptr;
		if (!Square)
		{
			continue;
		}

		if (Index < OwnedCount)
		{
			Square->SetFromSlot(Slots[Index], IconForSlot(Slots[Index]));
		}
		else
		{
			// Past the end of what the meta has bought. Not empty: empty means "you own this and
			// it holds nothing", and the two have to read differently or the padlock says nothing.
			Square->SetCell(EInventoryCellVisual::Locked, EInventorySlotKind::Empty, nullptr, 0, 0);
		}
	}
}

void UInventoryScreenWidget::RebuildAttachmentSlots(UPanelWidget* Container, AShooterWeapon* Weapon,
	int32 FreeSlots, int32 MaxSlots)
{
	if (!Container || !AttachmentSlotClass)
	{
		return;
	}

	// What is fitted, in mount order. That order is also the order the cost falls in: the first
	// FreeSlots parts are free and the rest are paying for a cell, which is the same subtraction
	// the inventory does, read the same way round.
	const TArray<TObjectPtr<UWeaponAttachmentDefinition>>* Mounted =
		Weapon ? &Weapon->GetInstalledAttachments() : nullptr;

	for (int32 Index = 0; Index < MaxSlots; ++Index)
	{
		UInventorySlotWidget* Square = GetOrCreateSquare(Container, Index, AttachmentSlotClass);
		if (!Square)
		{
			continue;
		}

		UWeaponAttachmentDefinition* Here =
			(Mounted && Mounted->IsValidIndex(Index)) ? (*Mounted)[Index].Get() : nullptr;

		// Three states and no fourth. Filled when something is in it; free when it is one of the
		// squares the meta has paid for; Paid otherwise, which is usable but takes a cell out of
		// the bag. Nothing here is ever Locked: a slot beyond the free ones is a price, not a wall.
		EInventoryCellVisual CellVisual = EInventoryCellVisual::Paid;
		if (Here)
		{
			CellVisual = EInventoryCellVisual::Filled;
		}
		else if (Index < FreeSlots)
		{
			CellVisual = EInventoryCellVisual::Empty;
		}

		Square->SetCell(CellVisual, Here ? EInventorySlotKind::Attachment : EInventorySlotKind::Empty,
			Here ? Here->Icon.Get() : nullptr, Here ? 1 : 0, Here ? 1 : 0);

		// The square's identity. A weapon square never gets a grid index, so it can neither be
		// confused for a cell of the bag nor start a drag out of one.
		Square->SetAttachmentTarget(Weapon, Here);

		Square->OnAttachmentInstall.BindUObject(this, &UInventoryScreenWidget::HandleAttachmentInstall);
		Square->OnAttachmentRemove.BindUObject(this, &UInventoryScreenWidget::HandleAttachmentRemove);
	}
}

void UInventoryScreenWidget::HandleAttachmentInstall(int32 FromIndex, AShooterWeapon* Weapon)
{
	AShooterCharacter* Character = BoundCharacter.Get();
	UInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	if (!Inventory || !Weapon)
	{
		return;
	}

	// Same rule as every other write from this screen: the grid is the server's, so a client asks
	// and the host calls straight through. @see RequestMove.
	if (Character->HasAuthority())
	{
		Inventory->InstallAttachmentFromSlot(FromIndex, Weapon);
	}
	else
	{
		Inventory->Server_InstallAttachmentFromSlot(FromIndex, Weapon);
	}
}

void UInventoryScreenWidget::HandleAttachmentRemove(AShooterWeapon* Weapon, EWeaponAttachmentType InType, int32 ToIndex)
{
	AShooterCharacter* Character = BoundCharacter.Get();
	UInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	if (!Inventory || !Weapon)
	{
		return;
	}

	// ToIndex is deliberately not used yet. The component puts a removed part in the first cell
	// that will take it, and honouring an exact cell would mean a second decision -- what to do
	// when that one is occupied -- that nothing has asked for. The parameter stays in the signature
	// because the gesture carries it and dropping it here would be the harder thing to add back.
	(void)ToIndex;

	if (Character->HasAuthority())
	{
		Inventory->UninstallAttachment(Weapon, InType);
	}
	else
	{
		Inventory->Server_UninstallAttachment(Weapon, InType);
	}
}

UInventorySlotWidget* UInventoryScreenWidget::GetOrCreateSquare(UPanelWidget* Container, int32 Index,
	TSubclassOf<UInventorySlotWidget> SquareClass)
{
	if (!Container || !SquareClass)
	{
		return nullptr;
	}

	if (Container->GetChildrenCount() > Index)
	{
		return Cast<UInventorySlotWidget>(Container->GetChildAt(Index));
	}

	UInventorySlotWidget* Square = CreateWidget<UInventorySlotWidget>(this, SquareClass);
	if (Square)
	{
		Container->AddChild(Square);
	}
	return Square;
}

UTexture2D* UInventoryScreenWidget::IconForSlot(const FInventorySlot& InSlot) const
{
	// InSlot, not Slot: UWidget already has a member called Slot, and this project builds warnings
	// as errors, so shadowing it is a compile failure (C4458).
	//
	// Ammo used to draw the gun it fed, because a cell could be the wrong rounds. With one pool
	// [author, 2026-09-01] there is no wrong ammo, so it takes the icon of its kind like everything
	// else does.

	// An attachment is the second kind whose icon is its own rather than its kind's, and for the
	// same reason: a scope and a suppressor in the bag are two different decisions, and one shared
	// "attachment" glyph would make them the same square. It is also the picture already shown on
	// the weapon, which is what the contract's ghost cell relies on -- the same mark in both places
	// is what says which cell is being held by which fitted part, with no connector drawn.
	if (InSlot.Kind == EInventorySlotKind::Attachment)
	{
		if (const UWeaponAttachmentDefinition* Def = Cast<UWeaponAttachmentDefinition>(InSlot.Payload))
		{
			if (UTexture2D* AttachIcon = Def->Icon.Get())
			{
				return AttachIcon;
			}
		}
	}

	return IconForKind(InSlot.Kind);
}

UTexture2D* UInventoryScreenWidget::IconForKind(EInventorySlotKind InKind) const
{
	switch (InKind)
	{
	case EInventorySlotKind::Currency:       return CurrencyIcon;
	case EInventorySlotKind::Ammo:           return AmmoIcon;
	case EInventorySlotKind::AbilityUpgrade: return AbilityUpgradeIcon;
	case EInventorySlotKind::Attachment:     return AttachmentIcon;
	default:                                 return nullptr;
	}
}
