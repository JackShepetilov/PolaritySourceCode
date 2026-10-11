// InventoryBarWidget.cpp

#include "Variant_Shooter/UI/InventoryBarWidget.h"

#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/Inventory/InventoryIconSettings.h"
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
	// Rows are weapon SLOTS (keys 1, 2 and 3), the same as the inventory screen's panels.
	AShooterWeapon* Slots[AShooterCharacter::WeaponSlotCount];
	for (int32 SlotIndex = 0; SlotIndex < AShooterCharacter::WeaponSlotCount; ++SlotIndex)
	{
		Slots[SlotIndex] = Character->FindOwnedWeaponInHotkeySlot(SlotIndex);
		RebuildWeaponRow(SlotIndex, Slots[SlotIndex], Inventory);
	}

	if (Inventory)
	{
		// The free mount count is the same for every gun; the slots and what is in them are each
		// gun's own.
		//
		// The corner's big plate is the gun IN HAND (its ammo is what the plate shows), and
		// FirstWeaponAttachments sits inside that plate, so it follows the gun in hand too; the other
		// containers show the other slots, in slot order. Drawing slot 0 here put the pistol's scope on
		// the SMG.
		int32 ActiveSlot = 0;
		if (const AShooterWeapon* const InHand = Character->GetCurrentWeapon())
		{
			for (int32 SlotIndex = 0; SlotIndex < AShooterCharacter::WeaponSlotCount; ++SlotIndex)
			{
				if (Slots[SlotIndex] == InHand)
				{
					ActiveSlot = SlotIndex;
				}
			}
		}
		const int32 FreeSlots = Inventory->GetFreeAttachmentSlots();
		RebuildAttachmentSlots(FirstWeaponAttachments, Slots[ActiveSlot], FreeSlots);

		UPanelWidget* const Others[] = { SecondWeaponAttachments.Get(), ThirdWeaponAttachments.Get() };
		int32 OtherIndex = 0;
		for (int32 SlotIndex = 0; SlotIndex < AShooterCharacter::WeaponSlotCount; ++SlotIndex)
		{
			if (SlotIndex != ActiveSlot && OtherIndex < UE_ARRAY_COUNT(Others))
			{
				RebuildAttachmentSlots(Others[OtherIndex++], Slots[SlotIndex], FreeSlots);
			}
		}
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

	// The Blueprint draws rows 0 and 1 only, and sends every other index down its row 1 branch: a
	// third row handed to it would be painted over the second gun. The third is drawn here.
	if (RowIndex < 2)
	{
		BP_SetWeaponRow(RowIndex, Weapon, Ammo, MagazineSize, Reserve, bInfinite, bIsEquipped);
		LearnRowColors(RowIndex, bIsEquipped);
	}
	else
	{
		DrawThirdRow(Weapon, Ammo, Reserve, bInfinite, bIsEquipped);
	}
}

void UInventoryBarWidget::LearnRowColors(int32 Row, bool bRowIsEquipped)
{
	const UImage* const Bg = Cast<UImage>(GetWidgetFromName(Row == 0 ? TEXT("Slot1NameBg") : TEXT("Slot2NameBg")));
	const UTextBlock* const Text = Cast<UTextBlock>(GetWidgetFromName(Row == 0 ? TEXT("Slot1NameText") : TEXT("Slot2NameText")));
	if (Bg)
	{
		(bRowIsEquipped ? ThirdEquippedBgColor : ThirdIdleBgColor) = Bg->GetColorAndOpacity();
	}
	if (Text)
	{
		(bRowIsEquipped ? ThirdEquippedTextColor : ThirdIdleTextColor) = Text->GetColorAndOpacity().GetSpecifiedColor();
	}
}

void UInventoryBarWidget::DrawThirdRow(AShooterWeapon* Weapon, int32 Ammo, int32 Reserve, bool bInfinite, bool bIsEquipped)
{
	if (UImage* const Bg = Cast<UImage>(GetWidgetFromName(TEXT("Slot3NameBg"))))
	{
		Bg->SetColorAndOpacity(bIsEquipped ? ThirdEquippedBgColor : ThirdIdleBgColor);
	}
	if (UTextBlock* const Name = Cast<UTextBlock>(GetWidgetFromName(TEXT("Slot3NameText"))))
	{
		Name->SetColorAndOpacity(FSlateColor(bIsEquipped ? ThirdEquippedTextColor : ThirdIdleTextColor));
		Name->SetText(Weapon ? Weapon->GetWeaponDisplayName() : FText::GetEmpty());
	}

	// The big plate is the gun in hand, whichever row that is.
	if (!bIsEquipped || !Weapon)
	{
		return;
	}
	if (UImage* const Icon = Cast<UImage>(GetWidgetFromName(TEXT("WeaponIcon"))))
	{
		Icon->SetBrushFromTexture(Weapon->GetIcon(), false);
		Icon->SetRenderOpacity(Weapon->GetIcon() ? 1.0f : 0.0f);
	}
	if (UImage* const Badge = Cast<UImage>(GetWidgetFromName(TEXT("AmmoTypeImage"))))
	{
		Badge->SetBrushFromTexture(Weapon->GetAmmoBadge(), false);
		Badge->SetColorAndOpacity(Weapon->GetAmmoColor());
		Badge->SetRenderOpacity(Weapon->GetAmmoBadge() ? 1.0f : 0.0f);
	}
	if (UTextBlock* const AmmoLabel = Cast<UTextBlock>(GetWidgetFromName(TEXT("AmmoText"))))
	{
		AmmoLabel->SetText(FText::FromString(FString::FromInt(Ammo)));
	}
	if (UTextBlock* const ReserveLabel = Cast<UTextBlock>(GetWidgetFromName(TEXT("ReserveText"))))
	{
		ReserveLabel->SetText(FText::FromString(FString::FromInt(Reserve)));
		ReserveLabel->SetRenderOpacity(bInfinite ? 0.0f : 1.0f);
	}
}

void UInventoryBarWidget::RebuildAttachmentSlots(UPanelWidget* Container, AShooterWeapon* Weapon, int32 FreeSlots)
{
	if (!Container || !AttachmentSlotClass)
	{
		return;
	}

	// Same squares as the overlay, in the same order. @see UInventoryScreenWidget::RebuildAttachmentSlots
	TArray<EWeaponAttachmentType> Types;
	UInventoryIconSettings::GetWeaponSlotTypes(Weapon, Types);

	for (int32 Index = 0; Index < Types.Num(); ++Index)
	{
		if (UInventorySlotWidget* Square = GetOrCreateSquare(Container, Index, AttachmentSlotClass))
		{
			// Deliberately NOT given an attachment target. Without one the square cannot start a drag
			// or take a drop, which is what keeps the corner a readout: fitting an attachment is a
			// decision, and decisions live behind the inventory key.
			Square->SetFromWeaponSlot(Weapon, Types[Index], FreeSlots);
		}
	}

	while (Container->GetChildrenCount() > Types.Num())
	{
		Container->RemoveChildAt(Container->GetChildrenCount() - 1);
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
