// ShopComponent.cpp

#include "Variant_Shooter/Shop/ShopComponent.h"

#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UObjectIterator.h"
#include "Upgrades/DispenserUpgradePool.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "Variant_Shooter/Buildables/BuildableActor.h"
#include "Variant_Shooter/Buildables/DispenserSlotMachineComponent.h"
#include "Variant_Shooter/Inventory/InventoryIconSettings.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/ShooterPlayerController.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Weapons/WeaponAttachmentDefinition.h"

namespace
{
	/** Credit is a float of magazines; a price is met when the account is this close to it. */
	constexpr float CreditEpsilon = 0.001f;

	const TCHAR* RarityName(EUpgradeRarity Rarity)
	{
		switch (Rarity)
		{
		case EUpgradeRarity::Rare: return TEXT("Rare");
		case EUpgradeRarity::Epic: return TEXT("Epic");
		case EUpgradeRarity::Legendary: return TEXT("Legendary");
		default: return TEXT("Common");
		}
	}
}

UShopComponent::UShopComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UShopComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// A teammate's shelf is not something another screen has a use for.
	DOREPLIFETIME_CONDITION(UShopComponent, Credit, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UShopComponent, Shelf, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UShopComponent, WeaponShelf, COND_OwnerOnly);
}

UShopComponent* UShopComponent::Get(const AShooterCharacter* Character)
{
	const APlayerState* const State = Character ? Character->GetPlayerState() : nullptr;
	return State ? State->FindComponentByClass<UShopComponent>() : nullptr;
}

AShooterCharacter* UShopComponent::GetCustomer() const
{
	const APlayerState* const State = Cast<APlayerState>(GetOwner());
	return State ? Cast<AShooterCharacter>(State->GetPawn()) : nullptr;
}

void UShopComponent::OnRep_Shop()
{
	OnShopChanged.Broadcast();
}

void UShopComponent::NotifyChanged()
{
	OnShopChanged.Broadcast();
}

FText UShopComponent::DescribeOffer(const FShopOffer& Offer)
{
	FString Name = GetNameSafe(Offer.Definition);
	if (const UUpgradeDefinition* const Upgrade = Cast<UUpgradeDefinition>(Offer.Definition))
	{
		Name = FString::Printf(TEXT("%s %d"), *Upgrade->DisplayName.ToString(), Offer.Level);
	}
	else if (const UWeaponAttachmentDefinition* const Attachment = Cast<UWeaponAttachmentDefinition>(Offer.Definition))
	{
		Name = Attachment->DisplayName.IsEmpty() ? Attachment->GetName() : Attachment->DisplayName.ToString();
	}
	else if (const UDispenserBuffDefinition* const Buff = Cast<UDispenserBuffDefinition>(Offer.Definition))
	{
		Name = FString::Printf(TEXT("%s +%d"), Buff->DisplayName.IsEmpty() ? *Buff->GetName() : *Buff->DisplayName.ToString(),
			FMath::RoundToInt(Buff->Amount));
	}
	return FText::FromString(FString::Printf(TEXT("[%s] %s"), RarityName(Offer.Rarity), *Name));
}

// ==================== Credit ====================

void UShopComponent::AddCredit(float Magazines)
{
	if (GetOwnerRole() != ROLE_Authority || Magazines <= 0.0f)
	{
		return;
	}
	Credit += Magazines;
	UE_LOG(LogTemp, Log, TEXT("[SHOP_DEBUG] %s: credit +%.2f = %.2f"), *GetNameSafe(GetOwner()), Magazines, Credit);
	NotifyChanged();
}

// ==================== The deck ====================

void UShopComponent::RebuildDeck(const UDispenserUpgradePool* Pool)
{
	Deck.Reset();
	if (!Pool)
	{
		return;
	}

	for (const FDispenserUpgradeSlot& Slot : Pool->Slots)
	{
		for (const FDispenserUpgradeEntry& Entry : Slot.Upgrades)
		{
			if (Entry.Upgrade && Entry.Weight > 0.0f)
			{
				Deck.AddUnique(Entry.Upgrade);
			}
		}
	}
	for (const FDispenserAttachmentEntry& Entry : Pool->Attachments)
	{
		if (Entry.Attachment && Entry.Weight > 0.0f)
		{
			Deck.AddUnique(Entry.Attachment);
		}
	}
	for (const FDispenserBuffEntry& Entry : Pool->Buffs)
	{
		if (Entry.Buff && Entry.Weight > 0.0f)
		{
			Deck.AddUnique(Entry.Buff);
		}
	}

	for (int32 Index = Deck.Num() - 1; Index > 0; --Index)
	{
		Deck.Swap(Index, FMath::RandRange(0, Index));
	}
	UE_LOG(LogTemp, Log, TEXT("[SHOP_DEBUG] %s: deck shuffled, %d cards"), *GetNameSafe(GetOwner()), Deck.Num());
}

bool UShopComponent::MakeOffer(const UDispenserUpgradePool* Pool, const AShooterCharacter* Customer, UObject* Definition,
	FShopOffer& OutOffer) const
{
	if (!Pool || !Customer || !Definition)
	{
		return false;
	}

	OutOffer = FShopOffer();
	OutOffer.Definition = Definition;

	if (UUpgradeDefinition* const Upgrade = Cast<UUpgradeDefinition>(Definition))
	{
		const UUpgradeManagerComponent* const Upgrades = Customer->GetUpgradeManager();
		FUpgradeOfferCard Card;
		if (!Upgrades || !Upgrades->BuildCardFor(Pool, Upgrade, Card))
		{
			return false;
		}
		// The ladder [author, 2026-10-08]: no level rarer than one step above the best the player has
		// in any upgrade. With nothing owned, common only.
		int32 Ceiling = static_cast<int32>(EUpgradeRarity::Common);
		for (const UUpgradeDefinition* const Have : Upgrades->GetAcquiredUpgrades())
		{
			if (Have)
			{
				Ceiling = FMath::Max(Ceiling, static_cast<int32>(Have->GetLevelRarity(Upgrades->GetUpgradeLevel(Have->UpgradeTag))) + 1);
			}
		}
		if (static_cast<int32>(Card.Rarity) > Ceiling)
		{
			return false;
		}
		OutOffer.Kind = EShopOfferKind::Upgrade;
		OutOffer.Level = Card.ToLevel;
		OutOffer.Rarity = Card.Rarity;
		OutOffer.Price = Pool->GetShopPrice(Card.Rarity);
		return true;
	}

	if (const UWeaponAttachmentDefinition* const Attachment = Cast<UWeaponAttachmentDefinition>(Definition))
	{
		// The same two rules the reels had: never the very part the player has, and within a family
		// only a tier above the best one owned.
		// And the ladder, per attachment type: with no part of this type anywhere on the player only a
		// common one shows, and each tier owned lets the next one show up.
		TArray<const UWeaponAttachmentDefinition*> Owned;
		UDispenserSlotMachineComponent::GatherOwnedAttachments(Customer, Owned);
		int32 Ceiling = static_cast<int32>(EUpgradeRarity::Common);
		for (const UWeaponAttachmentDefinition* const Have : Owned)
		{
			if (Have == Attachment
				|| (Have && Attachment->Family != NAME_None && Have->Family == Attachment->Family && Have->Rarity >= Attachment->Rarity))
			{
				return false;
			}
			if (Have && Have->Type == Attachment->Type)
			{
				Ceiling = FMath::Max(Ceiling, static_cast<int32>(Have->Rarity) + 1);
			}
		}
		if (static_cast<int32>(Attachment->Rarity) > Ceiling)
		{
			return false;
		}
		OutOffer.Kind = EShopOfferKind::Attachment;
		OutOffer.Rarity = Attachment->Rarity;
		OutOffer.Price = Pool->GetShopPrice(Attachment->Rarity);
		return true;
	}

	if (Cast<UDispenserBuffDefinition>(Definition))
	{
		OutOffer.Kind = EShopOfferKind::Buff;
		OutOffer.Price = Pool->ShopBuffPrice;
		return true;
	}

	return false;
}

void UShopComponent::DealShelf(const UDispenserUpgradePool* Pool, AShooterCharacter* Customer)
{
	Shelf.Reset();
	if (!Pool || !Customer)
	{
		return;
	}

	// Off the top until the shelf is full. A card with nothing to give this customer is passed over
	// and waits for the next shuffle like a dealt one. Two shuffles at most: a catalogue smaller than
	// the shelf would otherwise deal forever.
	int32 Shuffles = 0;
	while (Shelf.Num() < Pool->ShopShelfSize)
	{
		if (Deck.Num() == 0)
		{
			if (++Shuffles > 2)
			{
				break;
			}
			RebuildDeck(Pool);
			if (Deck.Num() == 0)
			{
				break;
			}
		}

		UObject* const Top = Deck.Pop();
		if (Shelf.ContainsByPredicate([Top](const FShopOffer& Shown) { return Shown.Definition == Top; }))
		{
			continue;
		}
		FShopOffer Offer;
		if (MakeOffer(Pool, Customer, Top, Offer))
		{
			Shelf.Add(Offer);
		}
	}
	bEverStocked = true;

	for (int32 Index = 0; Index < Shelf.Num(); ++Index)
	{
		UE_LOG(LogTemp, Log, TEXT("[SHOP_DEBUG] %s shelf %d: %s for %.1f"), *GetNameSafe(GetOwner()), Index,
			*DescribeOffer(Shelf[Index]).ToString(), Shelf[Index].Price);
	}
}

// ==================== The shelf for the guns carried ====================

uint32 UShopComponent::SignatureOf(const AShooterWeapon* Weapon)
{
	if (!Weapon)
	{
		return 0;
	}
	uint32 Hash = GetTypeHash(Weapon->GetClass());
	for (const UWeaponAttachmentDefinition* const Fitted : Weapon->GetInstalledAttachments())
	{
		// Order must not matter: the list is in the order parts were fitted.
		Hash ^= GetTypeHash(Fitted);
	}
	return Hash == 0 ? 1 : Hash;
}

void UShopComponent::RefreshWeaponShelf(const UDispenserUpgradePool* Pool, AShooterCharacter* Customer, bool bForce)
{
	if (!Pool || !Customer)
	{
		return;
	}

	WeaponSignatures.SetNumZeroed(AShooterCharacter::WeaponSlotCount);

	TArray<const UWeaponAttachmentDefinition*> Owned;
	UDispenserSlotMachineComponent::GatherOwnedAttachments(Customer, Owned);

	for (int32 Slot = 0; Slot < AShooterCharacter::WeaponSlotCount; ++Slot)
	{
		const AShooterWeapon* const Weapon = Customer->FindOwnedWeaponInHotkeySlot(Slot);
		const uint32 Signature = SignatureOf(Weapon);
		if (!bForce && Signature == WeaponSignatures[Slot])
		{
			continue;
		}
		WeaponSignatures[Slot] = Signature;
		WeaponShelf.RemoveAll([Slot](const FShopOffer& Shown) { return Shown.WeaponSlot == Slot; });
		if (!Weapon)
		{
			continue;
		}

		TArray<EWeaponAttachmentType> Types;
		UInventoryIconSettings::GetWeaponSlotTypes(Weapon, Types);

		TArray<UWeaponAttachmentDefinition*> Candidates;
		TArray<float> Weights;
		for (const FDispenserAttachmentEntry& Entry : Pool->Attachments)
		{
			UWeaponAttachmentDefinition* const Attachment = Entry.Attachment;
			if (!Attachment || Entry.Weight <= 0.0f || !Types.Contains(Attachment->Type)
				|| !Attachment->FitsWeapon(Weapon->GetClass()) || Owned.Contains(Attachment))
			{
				continue;
			}

			// The ladder, per type on this gun: nothing fitted shows common only, and each tier
			// fitted lets the next one show up. Never a step down inside one family.
			const UWeaponAttachmentDefinition* const Fitted = Weapon->GetAttachmentOfType(Attachment->Type);
			const int32 Ceiling = Fitted ? static_cast<int32>(Fitted->Rarity) + 1 : static_cast<int32>(EUpgradeRarity::Common);
			if (static_cast<int32>(Attachment->Rarity) > Ceiling)
			{
				continue;
			}
			if (Fitted && Attachment->Family != NAME_None && Attachment->Family == Fitted->Family && Attachment->Rarity <= Fitted->Rarity)
			{
				continue;
			}
			Candidates.Add(Attachment);
			Weights.Add(Entry.Weight);
		}

		for (int32 Pick = 0; Pick < Pool->ShopOffersPerWeapon && Candidates.Num() > 0; ++Pick)
		{
			float Total = 0.0f;
			for (const float Weight : Weights)
			{
				Total += Weight;
			}
			float Roll = FMath::FRandRange(0.0f, Total);
			int32 Chosen = Candidates.Num() - 1;
			for (int32 Index = 0; Index < Candidates.Num(); ++Index)
			{
				Roll -= Weights[Index];
				if (Roll <= 0.0f)
				{
					Chosen = Index;
					break;
				}
			}

			FShopOffer Offer;
			Offer.Kind = EShopOfferKind::Attachment;
			Offer.Definition = Candidates[Chosen];
			Offer.Rarity = Candidates[Chosen]->Rarity;
			Offer.Price = Pool->GetShopPrice(Offer.Rarity) * Pool->ShopWeaponShelfMarkup;
			Offer.WeaponSlot = Slot;
			WeaponShelf.Add(Offer);
			UE_LOG(LogTemp, Log, TEXT("[SHOP_DEBUG] %s weapon shelf, slot %d (%s): %s for %.1f"), *GetNameSafe(GetOwner()), Slot,
				*GetNameSafe(Weapon), *DescribeOffer(Offer).ToString(), Offer.Price);

			Candidates.RemoveAt(Chosen);
			Weights.RemoveAt(Chosen);
		}
	}
}

// ==================== Open, restock, buy ====================

void UShopComponent::OpenWith(const UDispenserUpgradePool* Pool, AShooterCharacter* Customer)
{
	if (GetOwnerRole() != ROLE_Authority || !Pool || !Customer)
	{
		return;
	}
	if (!bEverStocked)
	{
		// The first shelf is there when the shop is first seen. Every one after it is paid for.
		DealShelf(Pool, Customer);
	}
	RefreshWeaponShelf(Pool, Customer, false);
	NotifyChanged();
}

bool UShopComponent::Restock(const UDispenserUpgradePool* Pool, AShooterCharacter* Customer)
{
	if (GetOwnerRole() != ROLE_Authority || !Pool || !Customer)
	{
		return false;
	}
	if (Credit + CreditEpsilon < Pool->ShopRestockPrice)
	{
		UE_LOG(LogTemp, Log, TEXT("[SHOP_DEBUG] %s: restock refused, %.2f of %.2f"), *GetNameSafe(GetOwner()), Credit, Pool->ShopRestockPrice);
		return false;
	}
	Credit = FMath::Max(0.0f, Credit - Pool->ShopRestockPrice);
	DealShelf(Pool, Customer);
	RefreshWeaponShelf(Pool, Customer, true);
	NotifyChanged();
	return true;
}

bool UShopComponent::Buy(const UDispenserUpgradePool* Pool, AShooterCharacter* Customer, bool bWeaponShelf, int32 Index)
{
	if (GetOwnerRole() != ROLE_Authority || !Pool || !Customer)
	{
		return false;
	}

	TArray<FShopOffer>& From = bWeaponShelf ? WeaponShelf : Shelf;
	if (!From.IsValidIndex(Index) || From[Index].bSold || !From[Index].Definition)
	{
		return false;
	}
	FShopOffer& Offer = From[Index];
	if (Credit + CreditEpsilon < Offer.Price)
	{
		UE_LOG(LogTemp, Log, TEXT("[SHOP_DEBUG] %s: cannot afford %s, %.2f of %.2f"), *GetNameSafe(GetOwner()),
			*DescribeOffer(Offer).ToString(), Credit, Offer.Price);
		return false;
	}

	switch (Offer.Kind)
	{
	case EShopOfferKind::Upgrade:
	{
		// Built again: the player may have picked the same upgrade up elsewhere since the shelf was
		// dealt, and the level on the tag must be the level granted.
		UUpgradeManagerComponent* const Upgrades = Customer->GetUpgradeManager();
		FUpgradeOfferCard Card;
		if (!Upgrades || !Upgrades->BuildCardFor(Pool, Cast<UUpgradeDefinition>(Offer.Definition), Card) || Card.ToLevel != Offer.Level
			|| !Upgrades->GrantOfferCard(Card))
		{
			UE_LOG(LogTemp, Log, TEXT("[SHOP_DEBUG] %s: %s is stale, taken off the shelf"), *GetNameSafe(GetOwner()),
				*DescribeOffer(Offer).ToString());
			Offer.bSold = true;
			NotifyChanged();
			return false;
		}
		break;
	}
	case EShopOfferKind::Attachment:
		UDispenserSlotMachineComponent::DeliverAttachment(Cast<UWeaponAttachmentDefinition>(Offer.Definition), Customer);
		break;
	case EShopOfferKind::Buff:
		UDispenserSlotMachineComponent::ApplyBuff(Cast<UDispenserBuffDefinition>(Offer.Definition), Customer);
		break;
	}

	Credit = FMath::Max(0.0f, Credit - Offer.Price);
	Offer.bSold = true;
	UE_LOG(LogTemp, Log, TEXT("[SHOP_DEBUG] %s bought %s for %.2f, credit %.2f"), *GetNameSafe(GetOwner()),
		*DescribeOffer(Offer).ToString(), Offer.Price, Credit);
	NotifyChanged();
	return true;
}

const UDispenserUpgradePool* UShopComponent::ResolvePool(const ABuildableActor* Dispenser, const AShooterCharacter* Customer) const
{
	if (!Dispenser || !Customer || !Dispenser->GetSlotMachine())
	{
		return nullptr;
	}
	const float Reach = Dispenser->ServiceRadius + 150.0f;
	if (FVector::DistSquared(Customer->GetActorLocation(), Dispenser->GetActorLocation()) > FMath::Square(Reach))
	{
		return nullptr;
	}
	return Dispenser->GetSlotMachine()->GetPool();
}

void UShopComponent::Server_Open_Implementation(ABuildableActor* Dispenser)
{
	AShooterCharacter* const Customer = GetCustomer();
	OpenWith(ResolvePool(Dispenser, Customer), Customer);
}

void UShopComponent::Server_Buy_Implementation(ABuildableActor* Dispenser, bool bWeaponShelf, int32 Index)
{
	AShooterCharacter* const Customer = GetCustomer();
	Buy(ResolvePool(Dispenser, Customer), Customer, bWeaponShelf, Index);
}

void UShopComponent::Server_Restock_Implementation(ABuildableActor* Dispenser)
{
	AShooterCharacter* const Customer = GetCustomer();
	Restock(ResolvePool(Dispenser, Customer), Customer);
}

// ==================== Console (the host's own account, no dispenser needed) ====================

namespace ShopDebug
{
	static const UDispenserUpgradePool* AnyPool()
	{
		for (TObjectIterator<UDispenserUpgradePool> It; It; ++It)
		{
			if (!It->HasAnyFlags(RF_ClassDefaultObject))
			{
				return *It;
			}
		}
		return nullptr;
	}

	static bool Resolve(UWorld* World, UShopComponent*& OutShop, AShooterCharacter*& OutCustomer, const UDispenserUpgradePool*& OutPool)
	{
		const APlayerController* const Controller = CoopPlayers::GetLocalController(World);
		OutCustomer = Controller ? Cast<AShooterCharacter>(Controller->GetPawn()) : nullptr;
		OutShop = UShopComponent::Get(OutCustomer);
		OutPool = AnyPool();
		if (!OutShop || !OutPool)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SHOP_DEBUG] no account (%s) or no pool loaded (%s)"), *GetNameSafe(OutShop), *GetNameSafe(OutPool));
			return false;
		}
		return true;
	}

	static void List(const UShopComponent* Shop)
	{
		UE_LOG(LogTemp, Log, TEXT("[SHOP_DEBUG] credit %.2f"), Shop->GetCredit());
		for (int32 Index = 0; Index < Shop->GetShelf().Num(); ++Index)
		{
			const FShopOffer& Offer = Shop->GetShelf()[Index];
			UE_LOG(LogTemp, Log, TEXT("[SHOP_DEBUG]   shelf %d: %s  %.1f%s"), Index, *UShopComponent::DescribeOffer(Offer).ToString(),
				Offer.Price, Offer.bSold ? TEXT("  SOLD") : TEXT(""));
		}
		for (int32 Index = 0; Index < Shop->GetWeaponShelf().Num(); ++Index)
		{
			const FShopOffer& Offer = Shop->GetWeaponShelf()[Index];
			UE_LOG(LogTemp, Log, TEXT("[SHOP_DEBUG]   weapon %d (slot %d): %s  %.1f%s"), Index, Offer.WeaponSlot,
				*UShopComponent::DescribeOffer(Offer).ToString(), Offer.Price, Offer.bSold ? TEXT("  SOLD") : TEXT(""));
		}
	}

	static void Command(const TArray<FString>& Args, UWorld* World)
	{
		UShopComponent* Shop = nullptr;
		AShooterCharacter* Customer = nullptr;
		const UDispenserUpgradePool* Pool = nullptr;
		if (!Resolve(World, Shop, Customer, Pool))
		{
			return;
		}

		const FString Verb = Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("list"));
		const float Number = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.0f;
		if (Verb == TEXT("credit"))
		{
			Shop->AddCredit(Number);
		}
		else if (Verb == TEXT("restock"))
		{
			Shop->Restock(Pool, Customer);
		}
		else if (Verb == TEXT("buy"))
		{
			Shop->Buy(Pool, Customer, false, FMath::RoundToInt(Number));
		}
		else if (Verb == TEXT("buyw"))
		{
			Shop->Buy(Pool, Customer, true, FMath::RoundToInt(Number));
		}
		else if (Verb == TEXT("screen"))
		{
			// The feed key without the key: the shop screen of the nearest dispenser that sells.
			ABuildableActor* Nearest = nullptr;
			float BestSq = TNumericLimits<float>::Max();
			for (TActorIterator<ABuildableActor> It(World); It; ++It)
			{
				const float DistSq = FVector::DistSquared(It->GetActorLocation(), Customer->GetActorLocation());
				if (It->bFeedBuysShopCredit && It->GetSlotMachine() && It->IsActive() && DistSq < BestSq)
				{
					BestSq = DistSq;
					Nearest = *It;
				}
			}
			AShooterPlayerController* const Host = Cast<AShooterPlayerController>(Customer->GetController());
			const bool bOpened = Host && Nearest && Host->OpenShopScreen(Nearest);
			UE_LOG(LogTemp, Log, TEXT("[SHOP_DEBUG] screen at %s (%.0f cm): %s"), *GetNameSafe(Nearest), Nearest ? FMath::Sqrt(BestSq) : 0.0f,
				bOpened ? TEXT("opened") : TEXT("not opened"));
			return;
		}
		Shop->OpenWith(Pool, Customer);
		List(Shop);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GCmdPolarityShop(
	TEXT("polarity.shop"),
	TEXT("The host's shop account. polarity.shop [list | credit <magazines> | restock | buy <shelf index> | buyw <weapon shelf index> | screen]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ShopDebug::Command));
