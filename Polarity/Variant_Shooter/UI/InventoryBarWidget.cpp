// InventoryBarWidget.cpp

#include "Variant_Shooter/UI/InventoryBarWidget.h"

#include "Components/PanelWidget.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/UI/InventorySlotWidget.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

void UInventoryBarWidget::InitializeFor(AShooterCharacter* InCharacter)
{
	// Unbind first so a respawn can call this again without stacking delegates.
	Shutdown();

	if (!InCharacter)
	{
		return;
	}

	BoundCharacter = InCharacter;

	// The inventory still matters to a widget that draws no cells: the reserve round count and the
	// number of free attachment slots both come from it.
	if (UInventoryComponent* Inventory = InCharacter->GetInventoryComponent())
	{
		Inventory->OnInventoryChanged.AddDynamic(this, &UInventoryBarWidget::HandleInventoryChanged);
	}

	InCharacter->OnWeaponInventoryChanged.AddDynamic(this, &UInventoryBarWidget::HandleWeaponInventoryChanged);

	// The one that makes the numbers move. OnWeaponInventoryChanged only fires when the SET of
	// weapons changes, so without this the counter sits at whatever it was when the widget bound.
	InCharacter->OnBulletCountUpdated.AddDynamic(this, &UInventoryBarWidget::HandleBulletCount);

	Rebuild();
}

void UInventoryBarWidget::Shutdown()
{
	if (AShooterCharacter* Character = BoundCharacter.Get())
	{
		if (UInventoryComponent* Inventory = Character->GetInventoryComponent())
		{
			Inventory->OnInventoryChanged.RemoveDynamic(this, &UInventoryBarWidget::HandleInventoryChanged);
		}

		Character->OnWeaponInventoryChanged.RemoveDynamic(this, &UInventoryBarWidget::HandleWeaponInventoryChanged);
		Character->OnBulletCountUpdated.RemoveDynamic(this, &UInventoryBarWidget::HandleBulletCount);
	}

	BoundCharacter = nullptr;
}

void UInventoryBarWidget::NativeDestruct()
{
	Shutdown();
	Super::NativeDestruct();
}

void UInventoryBarWidget::HandleInventoryChanged()
{
	Rebuild();
}

void UInventoryBarWidget::HandleWeaponInventoryChanged()
{
	Rebuild();
}

void UInventoryBarWidget::HandleBulletCount(int32 MagazineSize, int32 Bullets)
{
	Rebuild();
}

// ==================== Rebuild ====================

void UInventoryBarWidget::Rebuild()
{
	AShooterCharacter* Character = BoundCharacter.Get();
	if (!Character)
	{
		return;
	}

	const UInventoryComponent* Inventory = Character->GetInventoryComponent();

	// A row is a SLOT, not a role: row 0 is always the first weapon owned and row 1 always the
	// second, whichever one is in hand.
	//
	// Sorting the equipped gun to the front was tried and it is wrong. It makes the two names swap
	// places on every switch, so the thing the player tracks out of the corner of their eye - where
	// their other gun is - moves. Apex keeps both names put and moves only the highlight, and
	// bIsEquipped is what carries that.
	//
	// Anything past the second weapon is not drawn: the corner has two slots, and a third gun is a
	// design question nobody has asked yet.
	const TArray<AShooterWeapon*>& Weapons = Character->GetOwnedWeapons();

	RebuildWeaponRow(0, Weapons.IsValidIndex(0) ? Weapons[0] : nullptr, Inventory);
	RebuildWeaponRow(1, Weapons.IsValidIndex(1) ? Weapons[1] : nullptr, Inventory);

	if (Inventory)
	{
		// Attachment slots are per weapon and the count is the same for both, so both rows get the
		// same numbers. What is fitted differs, so each row is given its own weapon.
		const int32 FreeSlots = Inventory->GetFreeAttachmentSlots();
		const int32 MaxSlots = Inventory->GetMaxFreeAttachmentSlots();
		RebuildAttachmentSlots(FirstWeaponAttachments,
			Weapons.IsValidIndex(0) ? Weapons[0] : nullptr, FreeSlots, MaxSlots);
		RebuildAttachmentSlots(SecondWeaponAttachments,
			Weapons.IsValidIndex(1) ? Weapons[1] : nullptr, FreeSlots, MaxSlots);
	}
}

void UInventoryBarWidget::RebuildWeaponRow(int32 RowIndex, AShooterWeapon* Weapon, const UInventoryComponent* Inventory)
{
	const int32 Ammo = Weapon ? Weapon->GetBulletCount() : 0;
	const int32 MagazineSize = Weapon ? Weapon->GetMagazineSize() : 0;

	// The reserve is not a number for two weapons: the one with no magazine at all, and the granted
	// one whose magazine is real but refills out of nowhere. HasFiniteReserve is the single question
	// that covers both, so there is no second "is infinite" rule to keep in sync.
	const bool bInfinite = Weapon && !Weapon->HasFiniteReserve();

	// The cells hold everything, loaded rounds included, so the number under the magazine is the
	// UNLOADED part. Showing the whole pool would count the rounds in the gun twice. The energy
	// reserve is already only the unloaded part.
	int32 Reserve = 0;
	if (Weapon && Weapon->UsesEnergyReserve())
	{
		Reserve = Weapon->GetEnergyReserve();
	}
	else if (Weapon && !bInfinite && Inventory)
	{
		Reserve = FMath::Max(0, Inventory->GetAmmo() - Ammo);
	}

	const AShooterCharacter* Character = BoundCharacter.Get();
	const bool bIsEquipped = Weapon && Character && Character->GetCurrentWeapon() == Weapon;

	BP_SetWeaponRow(RowIndex, Weapon, Ammo, MagazineSize, Reserve, bInfinite, bIsEquipped);
}

void UInventoryBarWidget::RebuildAttachmentSlots(UPanelWidget* Container, AShooterWeapon* Weapon,
	int32 FreeSlots, int32 MaxSlots)
{
	if (!Container || !AttachmentSlotClass)
	{
		return;
	}

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

		// Same three states as the overlay, read the same way: filled, free, or costing a cell.
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

		// Deliberately NOT given an attachment target. Without one the square cannot start a drag
		// or take a drop, which is what keeps the corner a readout: fitting an attachment is a
		// decision, and decisions live behind the inventory key.
	}
}

UInventorySlotWidget* UInventoryBarWidget::GetOrCreateSquare(UPanelWidget* Container, int32 Index,
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
