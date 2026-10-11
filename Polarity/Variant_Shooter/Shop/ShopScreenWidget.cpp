// ShopScreenWidget.cpp

#include "Variant_Shooter/Shop/ShopScreenWidget.h"

#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "PolarityPalette.h"
#include "Upgrades/DispenserUpgradePool.h"
#include "Upgrades/UpgradeDefinition.h"
#include "Variant_Shooter/Buildables/BuildableActor.h"
#include "Variant_Shooter/Buildables/BuilderComponent.h"
#include "Variant_Shooter/Buildables/DispenserSlotMachineComponent.h"
#include "Variant_Shooter/Buildables/TurretFeedWidget.h"
#include "Variant_Shooter/Shop/ShopComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Weapons/WeaponAttachmentDefinition.h"

namespace
{
	FText PriceText(float Price)
	{
		FNumberFormattingOptions Format;
		Format.SetMaximumFractionalDigits(1);
		return FText::AsNumber(Price, &Format);
	}

	/** The offer's name alone. The rarity goes on the detail line, not in front of the name. */
	FText NameOf(const FShopOffer& Offer)
	{
		if (const UUpgradeDefinition* const Upgrade = Cast<UUpgradeDefinition>(Offer.Definition))
		{
			return Offer.Level > 1
				? FText::Format(NSLOCTEXT("Shop", "UpgradeLevel", "{0}  lv {1}"), Upgrade->DisplayName, FText::AsNumber(Offer.Level))
				: Upgrade->DisplayName;
		}
		if (const UWeaponAttachmentDefinition* const Attachment = Cast<UWeaponAttachmentDefinition>(Offer.Definition))
		{
			return Attachment->DisplayName.IsEmpty() ? FText::FromString(Attachment->GetName()) : Attachment->DisplayName;
		}
		if (const UDispenserBuffDefinition* const Buff = Cast<UDispenserBuffDefinition>(Offer.Definition))
		{
			return FText::Format(NSLOCTEXT("Shop", "Buff", "{0} +{1}"),
				Buff->DisplayName.IsEmpty() ? FText::FromString(Buff->GetName()) : Buff->DisplayName, FText::AsNumber(FMath::RoundToInt(Buff->Amount)));
		}
		return FText::GetEmpty();
	}

	FText RarityText(EUpgradeRarity Rarity)
	{
		switch (Rarity)
		{
		case EUpgradeRarity::Rare: return NSLOCTEXT("Shop", "Rare", "Rare");
		case EUpgradeRarity::Epic: return NSLOCTEXT("Shop", "Epic", "Epic");
		case EUpgradeRarity::Legendary: return NSLOCTEXT("Shop", "Legendary", "Legendary");
		default: return NSLOCTEXT("Shop", "Common", "Common");
		}
	}

	UTexture2D* IconOf(const FShopOffer& Offer)
	{
		if (const UUpgradeDefinition* const Upgrade = Cast<UUpgradeDefinition>(Offer.Definition))
		{
			return Upgrade->Icon.Get();
		}
		if (const UWeaponAttachmentDefinition* const Attachment = Cast<UWeaponAttachmentDefinition>(Offer.Definition))
		{
			return Attachment->GetDisplayIcon();
		}
		if (const UDispenserBuffDefinition* const Buff = Cast<UDispenserBuffDefinition>(Offer.Definition))
		{
			return Buff->Icon.Get();
		}
		return nullptr;
	}
}

void UShopScreenWidget::InitializeFor(AShooterCharacter* InCharacter)
{
	Shutdown();
	if (!InCharacter)
	{
		return;
	}

	BoundCharacter = InCharacter;
	InCharacter->OnWeaponInventoryChanged.AddDynamic(this, &UShopScreenWidget::HandleWeaponInventoryChanged);
	SetVisibility(ESlateVisibility::Collapsed);
}

void UShopScreenWidget::Shutdown()
{
	Close();
	if (AShooterCharacter* const Character = BoundCharacter.Get())
	{
		Character->OnWeaponInventoryChanged.RemoveDynamic(this, &UShopScreenWidget::HandleWeaponInventoryChanged);
	}
	if (UShopComponent* const Shop = BoundShop.Get())
	{
		Shop->OnShopChanged.RemoveDynamic(this, &UShopScreenWidget::HandleShopChanged);
	}
	BoundCharacter = nullptr;
	BoundShop = nullptr;
}

void UShopScreenWidget::NativeDestruct()
{
	Shutdown();
	Super::NativeDestruct();
}

// ==================== Open and close ====================

void UShopScreenWidget::Open(ABuildableActor* InDispenser)
{
	AShooterCharacter* const Character = BoundCharacter.Get();
	if (bIsOpen || !Character || !InDispenser)
	{
		return;
	}

	// The account lives on the player state, which a client may not have had when the pawn was
	// bound, so it is looked up when the shop is opened and not before.
	UShopComponent* const Shop = UShopComponent::Get(Character);
	if (!Shop)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SHOP_DEBUG] %s has no shop account yet"), *Character->GetName());
		return;
	}
	if (BoundShop.Get() != Shop)
	{
		if (UShopComponent* const Old = BoundShop.Get())
		{
			Old->OnShopChanged.RemoveDynamic(this, &UShopScreenWidget::HandleShopChanged);
		}
		Shop->OnShopChanged.AddDynamic(this, &UShopScreenWidget::HandleShopChanged);
		BoundShop = Shop;
	}

	Dispenser = InDispenser;
	bIsOpen = true;

	// Deals the first shelf and lines the weapon shelf up with the guns carried now; the rows arrive
	// through OnShopChanged.
	Shop->Server_Open(InDispenser);

	Rebuild();
	SetVisibility(ESlateVisibility::Visible);

	if (APlayerController* const PC = GetOwningPlayer())
	{
		// GameAndUI, as the inventory overlay: the player must be able to walk away from a shop.
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
	}
	BP_OnOpened();
}

void UShopScreenWidget::Close()
{
	if (!bIsOpen)
	{
		return;
	}
	bIsOpen = false;
	Dispenser = nullptr;
	SetVisibility(ESlateVisibility::Collapsed);

	if (APlayerController* const PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
	BP_OnClosed();
}

void UShopScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bIsOpen)
	{
		return;
	}

	// Walked away, died, or the dispenser is gone: the counter is closed.
	const AShooterCharacter* const Character = BoundCharacter.Get();
	const ABuildableActor* const Counter = Dispenser.Get();
	const float Reach = Counter ? Counter->ServiceRadius + 150.0f : 0.0f;
	if (!Character || !Counter || Counter->IsDestroyed()
		|| FVector::DistSquared(Character->GetActorLocation(), Counter->GetActorLocation()) > FMath::Square(Reach))
	{
		Close();
		return;
	}

	// The row under the cursor is the bright one.
	// The cursor in desktop space, which is the space a cached geometry tests in.
	const int32 Hovered = FindRowUnder(FSlateApplication::Get().GetCursorPos());
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (UTurretFeedEntryWidget* const Widget = Rows[Index].Widget.Get())
		{
			Widget->SetRenderOpacity(Index == Hovered ? 1.0f : RestingRowOpacity);
		}
	}
}

// ==================== Rows ====================

void UShopScreenWidget::HandleShopChanged()
{
	if (bIsOpen)
	{
		Rebuild();
	}
}

void UShopScreenWidget::HandleWeaponInventoryChanged()
{
	if (!bIsOpen)
	{
		return;
	}
	// A gun was sold or picked up: the weapon shelf follows the guns, and only the server can redo it.
	if (UShopComponent* const Shop = BoundShop.Get())
	{
		Shop->Server_Open(Dispenser.Get());
	}
	Rebuild();
}

UTurretFeedEntryWidget* UShopScreenWidget::AddRow(UPanelWidget* Panel, ERowAction Action, int32 Index)
{
	if (!Panel || !EntryClass)
	{
		return nullptr;
	}
	UTurretFeedEntryWidget* const Entry = CreateWidget<UTurretFeedEntryWidget>(this, EntryClass);
	if (!Entry)
	{
		return nullptr;
	}
	Entry->AvailableColor = UPolarityPalette::GetColor(AvailableColorTag, FLinearColor::White);
	Entry->UnavailableColor = UPolarityPalette::GetColor(UnavailableColorTag, FLinearColor::Gray);
	Entry->PlateRestColor = UPolarityPalette::GetColor(PlateColorTag, FLinearColor::Black);
	Entry->SetRenderOpacity(RestingRowOpacity);
	if (UVerticalBoxSlot* const BoxSlot = Cast<UVerticalBoxSlot>(Panel->AddChild(Entry)))
	{
		BoxSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}

	FRow Row;
	Row.Widget = Entry;
	Row.Action = Action;
	Row.Index = Index;
	Rows.Add(Row);
	return Entry;
}

void UShopScreenWidget::AddOfferRow(UPanelWidget* Panel, ERowAction Action, int32 Index, const FShopOffer& Offer, const FText& Detail)
{
	UTurretFeedEntryWidget* const Entry = AddRow(Panel, Action, Index);
	const UShopComponent* const Shop = BoundShop.Get();
	if (!Entry || !Shop)
	{
		return;
	}

	const FText Line = Detail.IsEmpty() ? RarityText(Offer.Rarity)
		: FText::Format(NSLOCTEXT("Shop", "DetailFor", "{0}, for {1}"), RarityText(Offer.Rarity), Detail);
	Entry->SetupCard(Index + 1, NameOf(Offer), Line, IconOf(Offer));
	const bool bAffordable = !Offer.bSold && Shop->GetCredit() + 0.001f >= Offer.Price;
	Rows.Last().bAffordable = bAffordable;
	Entry->SetState(bAffordable ? ETurretFeedEntryState::Available : ETurretFeedEntryState::Unavailable,
		Offer.bSold ? NSLOCTEXT("Shop", "Sold", "SOLD") : PriceText(Offer.Price));
}

void UShopScreenWidget::Rebuild()
{
	for (const FRow& Row : Rows)
	{
		if (UTurretFeedEntryWidget* const Widget = Row.Widget.Get())
		{
			Widget->RemoveFromParent();
		}
	}
	Rows.Reset();

	const AShooterCharacter* const Character = BoundCharacter.Get();
	const UShopComponent* const Shop = BoundShop.Get();
	const ABuildableActor* const Counter = Dispenser.Get();
	if (!Character || !Shop || !Counter)
	{
		return;
	}
	const UDispenserUpgradePool* const Pool = Counter->GetSlotMachine() ? Counter->GetSlotMachine()->GetPool() : nullptr;

	if (CreditText)
	{
		CreditText->SetText(PriceText(Shop->GetCredit()));
	}

	// The guns carried, by slot: what each would sell for, priced the way the server will price it.
	for (int32 GunSlot = 0; GunSlot < AShooterCharacter::WeaponSlotCount; ++GunSlot)
	{
		AShooterWeapon* const Weapon = Character->FindOwnedWeaponInHotkeySlot(GunSlot);
		if (!Weapon || Weapon->IsMeleeWeapon())
		{
			continue;
		}
		if (UTurretFeedEntryWidget* const Entry = AddRow(SellPanel, ERowAction::Sell, GunSlot))
		{
			Rows.Last().Weapon = Weapon;
			const float Worth = Weapon->GetDepositMagazines(Weapon->GetBulletCount(), FMath::Max(0, Weapon->GetEnergyReserve()))
				* (Pool ? Pool->ShopCreditPerMagazine : 1.0f);
			Entry->Setup(Weapon, GunSlot + 1);
			Entry->SetState(ETurretFeedEntryState::Available,
				FText::Format(NSLOCTEXT("Shop", "SellFor", "+{0}"), PriceText(Worth)));
		}
	}

	for (int32 Index = 0; Index < Shop->GetShelf().Num(); ++Index)
	{
		AddOfferRow(ShelfPanel, ERowAction::Buy, Index, Shop->GetShelf()[Index], FText::GetEmpty());
	}

	for (int32 Index = 0; Index < Shop->GetWeaponShelf().Num(); ++Index)
	{
		const FShopOffer& Offer = Shop->GetWeaponShelf()[Index];
		const AShooterWeapon* const For = Character->FindOwnedWeaponInHotkeySlot(Offer.WeaponSlot);
		AddOfferRow(WeaponShelfPanel, ERowAction::BuyForWeapon, Index, Offer, For ? For->GetWeaponDisplayName() : FText::GetEmpty());
	}

	if (UTurretFeedEntryWidget* const Entry = AddRow(RestockPanel, ERowAction::Restock, 0))
	{
		const float Price = Pool ? Pool->ShopRestockPrice : 0.0f;
		const bool bAffordable = Shop->GetCredit() + 0.001f >= Price;
		Rows.Last().bAffordable = bAffordable;
		Entry->SetupCard(0, NSLOCTEXT("Shop", "Restock", "Restock"), NSLOCTEXT("Shop", "RestockDetail", "a new shelf"), nullptr);
		Entry->SetState(bAffordable ? ETurretFeedEntryState::Available : ETurretFeedEntryState::Unavailable, PriceText(Price));
	}
}

int32 UShopScreenWidget::FindRowUnder(const FVector2D& ScreenPosition) const
{
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const UTurretFeedEntryWidget* const Widget = Rows[Index].Widget.Get();
		if (Widget && Widget->GetCachedGeometry().IsUnderLocation(ScreenPosition))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void UShopScreenWidget::Activate(const FRow& Row)
{
	AShooterCharacter* const Character = BoundCharacter.Get();
	UShopComponent* const Shop = BoundShop.Get();
	ABuildableActor* const Counter = Dispenser.Get();
	if (!Character || !Shop || !Counter)
	{
		return;
	}

	if (!Row.bAffordable)
	{
		if (UTurretFeedEntryWidget* const Widget = Row.Widget.Get())
		{
			Widget->FlashRefused(UPolarityPalette::GetColor(RefusedFlashTag, FLinearColor::Red));
		}
		return;
	}

	switch (Row.Action)
	{
	case ERowAction::Sell:
		if (UBuilderComponent* const Builder = Character->FindComponentByClass<UBuilderComponent>())
		{
			Builder->SellWeaponToDispenser(Counter, Row.Weapon.Get());
		}
		break;
	case ERowAction::Buy:
		Shop->Server_Buy(Counter, false, Row.Index);
		break;
	case ERowAction::BuyForWeapon:
		Shop->Server_Buy(Counter, true, Row.Index);
		break;
	case ERowAction::Restock:
		Shop->Server_Restock(Counter);
		break;
	}
}

FReply UShopScreenWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsOpen && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const int32 Index = FindRowUnder(InMouseEvent.GetScreenSpacePosition());
		if (Rows.IsValidIndex(Index))
		{
			// A copy: the click may rebuild the rows before Activate returns (the host is the server).
			const FRow Row = Rows[Index];
			Activate(Row);
		}
		// Swallowed either way: a click on the shop must not fire the gun underneath.
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UShopScreenWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bIsOpen && (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F))
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
