// InventoryScreenWidget.cpp

#include "Variant_Shooter/UI/InventoryScreenWidget.h"

#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Upgrades/DispenserUpgradePool.h"
#include "Upgrades/UpgradeDefinition.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "GameFramework/PlayerController.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/Inventory/InventoryIconSettings.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/UI/InventoryDragDropOperation.h"
#include "Variant_Shooter/UI/InventorySlotWidget.h"
#include "Variant_Shooter/Siege/MechPartDefinition.h"
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

	// The action slots follow the upgrades themselves: on a client those change through the
	// manager's RPC, not through the inventory's replication.
	if (UUpgradeManagerComponent* Upgrades = InCharacter->GetUpgradeManager())
	{
		Upgrades->OnUpgradeGranted.AddDynamic(this, &UInventoryScreenWidget::HandleUpgradeChanged);
		Upgrades->OnUpgradeRemoved.AddDynamic(this, &UInventoryScreenWidget::HandleUpgradeChanged);
		Upgrades->OnUpgradeLeveledUp.AddDynamic(this, &UInventoryScreenWidget::HandleUpgradeLeveled);
	}

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

		if (UUpgradeManagerComponent* Upgrades = Character->GetUpgradeManager())
		{
			Upgrades->OnUpgradeGranted.RemoveDynamic(this, &UInventoryScreenWidget::HandleUpgradeChanged);
			Upgrades->OnUpgradeRemoved.RemoveDynamic(this, &UInventoryScreenWidget::HandleUpgradeChanged);
			Upgrades->OnUpgradeLeveledUp.RemoveDynamic(this, &UInventoryScreenWidget::HandleUpgradeLeveled);
		}
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

	AShooterCharacter* const Character = BoundCharacter.Get();
	const int32 Panel = FindPanelUnder(InDragDropEvent.GetScreenSpacePosition());
	AShooterWeapon* const PanelWeapon = GetWeaponInPanel(Panel);

	// A whole gun: onto the other panel it trades slots, onto its own it stays, anywhere else it goes
	// on the floor, Apex-style.
	if (Dragged->DraggedWeapon)
	{
		if (!Character)
		{
			return true;
		}
		if (Panel != INDEX_NONE)
		{
			if (PanelWeapon != Dragged->DraggedWeapon)
			{
				if (Character->HasAuthority())
				{
					Character->SwapWeaponSlots();
				}
				else
				{
					Character->Server_SwapWeaponSlots();
				}
			}
			return true;
		}
		if (Character->HasAuthority())
		{
			Character->DropWeaponFromInventory(Dragged->DraggedWeapon);
		}
		else
		{
			Character->Server_DropWeaponFromInventory(Dragged->DraggedWeapon);
		}
		return true;
	}

	// A part off a gun: onto the other gun's panel it moves there (into the slot of its type),
	// onto its own panel nothing happens, anywhere else it goes on the floor. The bag's cells catch
	// their own drops, so reaching here means the cursor was not over the bag.
	if (Dragged->SourceWeapon)
	{
		if (Panel != INDEX_NONE)
		{
			if (PanelWeapon && PanelWeapon != Dragged->SourceWeapon)
			{
				HandleAttachmentMove(Dragged->SourceWeapon, Dragged->SourceAttachmentType, PanelWeapon);
			}
			return true;
		}
		RequestDropMountedAttachment(Dragged->SourceWeapon, Dragged->SourceAttachmentType);
		return true;
	}

	// A bag cell over a weapon panel: an attachment goes into the slot of its type on that gun;
	// anything else is not for a gun, and the drop is swallowed rather than read as "throw away".
	if (Dragged->SourceIndex != INDEX_NONE && Panel != INDEX_NONE)
	{
		if (PanelWeapon && Dragged->DraggedAttachment)
		{
			HandleAttachmentInstall(Dragged->SourceIndex, PanelWeapon);
		}
		return true;
	}

	// Dragged out of an action slot and let go anywhere that is not a cell: take it off into the
	// first free cell. Same reasoning as the attachment above, it is not in the bag yet.
	if (Dragged->SourceUpgradeSlot != INDEX_NONE)
	{
		HandleUpgradeUnequip(Dragged->SourceUpgradeSlot, INDEX_NONE);
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
	RebuildActionSlots();

	// The free mount count is per weapon and the same for both; which slots each gun has, and
	// what is in them, is the gun's own.
	const int32 FreeSlots = Inventory->GetFreeAttachmentSlots();

	RebuildAttachmentSlots(FirstWeaponAttachments, GetWeaponInPanel(0), FreeSlots);
	RebuildAttachmentSlots(SecondWeaponAttachments, GetWeaponInPanel(1), FreeSlots);
}

AShooterWeapon* UInventoryScreenWidget::GetWeaponInPanel(int32 PanelIndex) const
{
	const AShooterCharacter* const Character = BoundCharacter.Get();
	return (Character && PanelIndex != INDEX_NONE) ? Character->FindOwnedWeaponInHotkeySlot(PanelIndex) : nullptr;
}

int32 UInventoryScreenWidget::FindPanelUnder(const FVector2D& ScreenPosition) const
{
	const UWidget* const Panels[2] = { WeaponPanel1.Get(), WeaponPanel2.Get() };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (Panels[Index] && Panels[Index]->IsVisible() && Panels[Index]->GetCachedGeometry().IsUnderLocation(ScreenPosition))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void UInventoryScreenWidget::GatherAttachmentSquares(TArray<UInventorySlotWidget*>& OutSquares) const
{
	OutSquares.Reset();
	for (const UPanelWidget* const Container : { FirstWeaponAttachments.Get(), SecondWeaponAttachments.Get() })
	{
		if (!Container)
		{
			continue;
		}
		for (int32 Index = 0; Index < Container->GetChildrenCount(); ++Index)
		{
			if (UInventorySlotWidget* const Square = Cast<UInventorySlotWidget>(Container->GetChildAt(Index)))
			{
				OutSquares.Add(Square);
			}
		}
	}
}

FReply UInventoryScreenWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Squares handle their own presses; what reaches the screen was on a panel's plate or on nothing.
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const int32 Panel = FindPanelUnder(InMouseEvent.GetScreenSpacePosition());
		if (GetWeaponInPanel(Panel))
		{
			PressedWeaponPanel = Panel;
			return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
		}
	}
	PressedWeaponPanel = INDEX_NONE;
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UInventoryScreenWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
	UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);

	AShooterWeapon* const Weapon = GetWeaponInPanel(PressedWeaponPanel);
	PressedWeaponPanel = INDEX_NONE;
	if (!Weapon)
	{
		return;
	}

	UInventoryDragDropOperation* const Operation = NewObject<UInventoryDragDropOperation>(GetTransientPackage());
	Operation->Pivot = EDragPivot::CenterCenter;
	Operation->DraggedWeapon = Weapon;

	// The gun's own picture on a grid square, so the thing under the cursor reads as that gun.
	if (GridCellClass)
	{
		if (UInventorySlotWidget* const Ghost = CreateWidget<UInventorySlotWidget>(this, GridCellClass))
		{
			Ghost->SetRarityTint(false);
			Ghost->SetCell(EInventoryCellVisual::Filled, EInventorySlotKind::Empty, Weapon->GetIcon(), 1, 1);
			Operation->DefaultDragVisual = Ghost;
		}
	}
	OutOperation = Operation;
}

void UInventoryScreenWidget::HandleDragStarted(UDragDropOperation* Operation)
{
	if (!Operation)
	{
		return;
	}
	Operation->OnDrop.AddUniqueDynamic(this, &UInventoryScreenWidget::HandleDragEnded);
	Operation->OnDragCancelled.AddUniqueDynamic(this, &UInventoryScreenWidget::HandleDragEnded);

	TArray<UInventorySlotWidget*> Squares;
	GatherAttachmentSquares(Squares);
	for (UInventorySlotWidget* const Square : Squares)
	{
		Square->SetCompatibleHint(Square->WouldAcceptDrag(Operation));
	}
}

void UInventoryScreenWidget::HandleDragEnded(UDragDropOperation* Operation)
{
	TArray<UInventorySlotWidget*> Squares;
	GatherAttachmentSquares(Squares);
	for (UInventorySlotWidget* const Square : Squares)
	{
		Square->SetCompatibleHint(false);
	}
}

void UInventoryScreenWidget::RebuildWeaponPanels(const UInventoryComponent& Inventory)
{
	AShooterCharacter* Character = BoundCharacter.Get();
	if (!Character)
	{
		return;
	}

	// A panel is a SLOT: panel 0 is key 1 and panel 1 key 2, and neither moves when the player
	// switches - the labels read "1 WEAPON" and "2 WEAPON", which only means anything if the slots
	// stay put. bIsEquipped is what says which one is in hand.
	const AShooterWeapon* Equipped = Character->GetCurrentWeapon();

	AShooterWeapon* Panels[2] = { GetWeaponInPanel(0), GetWeaponInPanel(1) };
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
			Square->OnCellClicked.BindUObject(this, &UInventoryScreenWidget::HandleCellClicked);
			Square->OnDragStarted.BindUObject(this, &UInventoryScreenWidget::HandleDragStarted);

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

void UInventoryScreenWidget::RebuildAttachmentSlots(UPanelWidget* Container, AShooterWeapon* Weapon, int32 FreeSlots)
{
	if (!Container || !AttachmentSlotClass)
	{
		return;
	}

	// One square per slot the gun HAS, in the one order the whole game uses (Apex: muzzle,
	// magazine, optic, stock). @see UInventoryIconSettings
	TArray<EWeaponAttachmentType> Types;
	UInventoryIconSettings::GetWeaponSlotTypes(Weapon, Types);

	for (int32 Index = 0; Index < Types.Num(); ++Index)
	{
		UInventorySlotWidget* Square = GetOrCreateSquare(Container, Index, AttachmentSlotClass);
		if (!Square)
		{
			continue;
		}

		Square->SetFromWeaponSlot(Weapon, Types[Index], FreeSlots);

		// The square's identity. A weapon square never gets a grid index, so it can neither be
		// confused for a cell of the bag nor start a drag out of one.
		Square->SetAttachmentTarget(Weapon, Weapon ? Weapon->GetAttachmentOfType(Types[Index]) : nullptr, Types[Index]);

		Square->OnAttachmentInstall.BindUObject(this, &UInventoryScreenWidget::HandleAttachmentInstall);
		Square->OnAttachmentRemove.BindUObject(this, &UInventoryScreenWidget::HandleAttachmentRemove);
		Square->OnAttachmentMove.BindUObject(this, &UInventoryScreenWidget::HandleAttachmentMove);
		Square->OnAttachmentClicked.BindUObject(this, &UInventoryScreenWidget::HandleAttachmentClicked);
		Square->OnDragStarted.BindUObject(this, &UInventoryScreenWidget::HandleDragStarted);
		// Hit-testable, or the cursor passes through and neither drag nor drop works. @see RebuildGrid
		Square->SetVisibility(ESlateVisibility::Visible);
	}

	// A gun with fewer slots than the last one drawn here.
	while (Container->GetChildrenCount() > Types.Num())
	{
		Container->RemoveChildAt(Container->GetChildrenCount() - 1);
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
	// and the host calls straight through. @see RequestMove. The Apex version: a part already in that
	// slot swaps back into the bag instead of refusing.
	if (Character->HasAuthority())
	{
		Inventory->PlaceAttachmentFromSlot(FromIndex, Weapon);
	}
	else
	{
		Inventory->Server_PlaceAttachmentFromSlot(FromIndex, Weapon);
	}
}

void UInventoryScreenWidget::HandleAttachmentMove(AShooterWeapon* From, EWeaponAttachmentType InType, AShooterWeapon* To)
{
	AShooterCharacter* Character = BoundCharacter.Get();
	UInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	if (!Inventory || !From || !To)
	{
		return;
	}
	if (Character->HasAuthority())
	{
		Inventory->MoveAttachmentBetweenWeapons(From, InType, To);
	}
	else
	{
		Inventory->Server_MoveAttachmentBetweenWeapons(From, InType, To);
	}
}

void UInventoryScreenWidget::RequestDropMountedAttachment(AShooterWeapon* Weapon, EWeaponAttachmentType InType)
{
	AShooterCharacter* Character = BoundCharacter.Get();
	UInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	if (!Inventory || !Weapon)
	{
		return;
	}
	if (Character->HasAuthority())
	{
		Inventory->DropMountedAttachmentToWorld(Weapon, InType);
	}
	else
	{
		Inventory->Server_DropMountedAttachmentToWorld(Weapon, InType);
	}
}

void UInventoryScreenWidget::HandleCellClicked(int32 GridIndex, bool bRight)
{
	AShooterCharacter* Character = BoundCharacter.Get();
	UInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	if (!Inventory || !Inventory->GetSlots().IsValidIndex(GridIndex))
	{
		return;
	}

	if (bRight)
	{
		RequestDropToWorld(GridIndex);
		return;
	}

	// Left click on an attachment puts it on a gun that takes it. Nothing else has a use on click.
	if (Inventory->GetSlots()[GridIndex].Kind == EInventorySlotKind::Attachment)
	{
		if (Character->HasAuthority())
		{
			Inventory->QuickEquipAttachmentFromSlot(GridIndex);
		}
		else
		{
			Inventory->Server_QuickEquipAttachmentFromSlot(GridIndex);
		}
	}
}

void UInventoryScreenWidget::HandleAttachmentClicked(AShooterWeapon* Weapon, EWeaponAttachmentType InType, bool bRight)
{
	if (bRight)
	{
		HandleAttachmentRemove(Weapon, InType, INDEX_NONE);
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

	// Into the cell it was dropped on: an empty one takes it, one holding a part of the same type
	// that fits trades places with it, anything else falls back to the first empty cell.
	if (Character->HasAuthority())
	{
		Inventory->UnmountAttachmentToSlot(Weapon, InType, ToIndex);
	}
	else
	{
		Inventory->Server_UnmountAttachmentToSlot(Weapon, InType, ToIndex);
	}
}

// ==================== Action slots ====================

void UInventoryScreenWidget::HandleUpgradeChanged(UUpgradeDefinition* Definition)
{
	RebuildActionSlots();
}

void UInventoryScreenWidget::HandleUpgradeLeveled(UUpgradeDefinition* Definition, int32 NewLevel)
{
	RebuildActionSlots();
}

void UInventoryScreenWidget::RebuildActionSlots()
{
	AShooterCharacter* Character = BoundCharacter.Get();
	const UUpgradeManagerComponent* Upgrades = Character ? Character->GetUpgradeManager() : nullptr;
	const UDispenserUpgradePool* Layout = Upgrades ? Upgrades->GetSlotLayout() : nullptr;
	const TSubclassOf<UInventorySlotWidget> SquareClass = ActionSlotClass ? ActionSlotClass : GridCellClass;
	if (!ActionSlots || !Layout || !SquareClass || !WidgetTree)
	{
		return;
	}

	// Only the exclusive slots are action slots; a non-exclusive one is a bag of passives that sit
	// side by side and has nothing to drag in or out.
	TArray<int32> SlotIndices;
	for (int32 Index = 0; Index < Layout->Slots.Num(); ++Index)
	{
		if (Layout->Slots[Index].bExclusive)
		{
			SlotIndices.Add(Index);
		}
	}

	// Same rule as the grid: rebuilt only when the shape changes, otherwise the squares are reused
	// and only their contents move.
	if (ActionSquareSlotIndices != SlotIndices || ActionSquares.Num() != SlotIndices.Num())
	{
		ActionSlots->ClearChildren();
		ActionSquares.Reset();
		ActionSquareSlotIndices = SlotIndices;

		for (const int32 SlotIndex : SlotIndices)
		{
			UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
			UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			UInventorySlotWidget* Square = CreateWidget<UInventorySlotWidget>(this, SquareClass);
			if (!Column || !Label || !Square)
			{
				ActionSquares.Add(nullptr);
				continue;
			}

			Label->SetText(Layout->Slots[SlotIndex].DisplayName.ToUpper());
			Label->SetJustification(ETextJustify::Center);
			FSlateFontInfo Font = Label->GetFont();
			Font.Size = 10;
			Label->SetFont(Font);
			Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.78f, 0.80f, 1.0f)));

			if (UVerticalBoxSlot* LabelSlot = Column->AddChildToVerticalBox(Label))
			{
				LabelSlot->SetHorizontalAlignment(HAlign_Center);
				LabelSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
			}
			if (UVerticalBoxSlot* SquareSlot = Column->AddChildToVerticalBox(Square))
			{
				SquareSlot->SetHorizontalAlignment(HAlign_Center);
			}
			ActionSlots->AddChild(Column);

			// Hit-testable for the same reason a grid cell is forced to be. @see RebuildGrid
			Square->SetVisibility(ESlateVisibility::Visible);
			Square->OnUpgradeEquip.BindUObject(this, &UInventoryScreenWidget::HandleUpgradeEquip);
			ActionSquares.Add(Square);
		}
	}

	for (int32 Index = 0; Index < ActionSquares.Num(); ++Index)
	{
		UInventorySlotWidget* Square = ActionSquares[Index].Get();
		if (!Square)
		{
			continue;
		}

		int32 Level = 0;
		UUpgradeDefinition* Equipped = Upgrades->GetOwnedInSlot(Layout, ActionSquareSlotIndices[Index], Level);
		UTexture2D* Icon = Equipped ? (Equipped->Icon ? Equipped->Icon.Get() : AbilityUpgradeIcon.Get()) : nullptr;
		Square->SetRarityTint(Equipped != nullptr, Equipped ? Equipped->GetLevelRarity(Level) : EUpgradeRarity::Common);
		Square->SetCell(Equipped ? EInventoryCellVisual::Filled : EInventoryCellVisual::Empty,
			Equipped ? EInventorySlotKind::AbilityUpgrade : EInventorySlotKind::Empty,
			Icon, Level, Level);
		Square->SetUpgradeSlotTarget(ActionSquareSlotIndices[Index], Layout, Equipped);
	}

	// An upgrade dragged OUT of a slot lands on a grid square, so every grid square listens for it.
	for (UInventorySlotWidget* Square : GridSquares)
	{
		if (Square)
		{
			Square->OnUpgradeUnequip.BindUObject(this, &UInventoryScreenWidget::HandleUpgradeUnequip);
		}
	}
}

void UInventoryScreenWidget::HandleUpgradeEquip(int32 FromIndex, int32 SlotIndex)
{
	AShooterCharacter* Character = BoundCharacter.Get();
	UUpgradeManagerComponent* Upgrades = Character ? Character->GetUpgradeManager() : nullptr;
	if (!Upgrades)
	{
		return;
	}

	// The cell says which slot its upgrade belongs to; SlotIndex is only where the cursor was.
	// Authority split as in RequestMove.
	if (Character->HasAuthority())
	{
		Upgrades->EquipFromBag(FromIndex);
	}
	else
	{
		Upgrades->Server_EquipFromBag(FromIndex);
	}
}

void UInventoryScreenWidget::HandleUpgradeUnequip(int32 SlotIndex, int32 ToIndex)
{
	AShooterCharacter* Character = BoundCharacter.Get();
	UUpgradeManagerComponent* Upgrades = Character ? Character->GetUpgradeManager() : nullptr;
	if (!Upgrades)
	{
		return;
	}

	if (Character->HasAuthority())
	{
		Upgrades->UnequipToBag(SlotIndex, ToIndex);
	}
	else
	{
		Upgrades->Server_UnequipToBag(SlotIndex, ToIndex);
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
	// An upgrade wears its own icon too: which upgrade it is decides where it can be dragged.
	if (InSlot.Kind == EInventorySlotKind::AbilityUpgrade)
	{
		if (const UUpgradeDefinition* Def = Cast<UUpgradeDefinition>(InSlot.Payload))
		{
			if (UTexture2D* UpgradeIcon = Def->Icon.Get())
			{
				return UpgradeIcon;
			}
		}
	}

	if (InSlot.Kind == EInventorySlotKind::Attachment)
	{
		if (const UWeaponAttachmentDefinition* Def = Cast<UWeaponAttachmentDefinition>(InSlot.Payload))
		{
			if (UTexture2D* AttachIcon = Def->GetDisplayIcon())
			{
				return AttachIcon;
			}
		}
	}

	// A mech part wears its own picture: which slot it fills is the whole question for the barn.
	if (InSlot.Kind == EInventorySlotKind::MechPart)
	{
		if (const UMechPartDefinition* Def = Cast<UMechPartDefinition>(InSlot.Payload))
		{
			if (UTexture2D* PartIcon = Def->Icon.LoadSynchronous())
			{
				return PartIcon;
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
