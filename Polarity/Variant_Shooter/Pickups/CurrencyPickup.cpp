// CurrencyPickup.cpp

#include "Variant_Shooter/Pickups/CurrencyPickup.h"

#include "Variant_Shooter/Inventory/InventoryComponent.h"

ACurrencyPickup::ACurrencyPickup()
{
	// Everything except the amount is fixed for money: it is the one kind with no definition asset
	// behind it, because a unit of money is a number rather than a thing.
	Item.Kind = EInventorySlotKind::Currency;
	Item.Payload = nullptr;
	Item.Count = 30;
	Item.StackMax = 100;
}

void ACurrencyPickup::BeginPlay()
{
	// Before Super, so the replicated Item is already right when BeginPlay broadcasts it.
	if (HasAuthority())
	{
		Item.Kind = EInventorySlotKind::Currency;
		Item.Payload = nullptr;
		Item.Count = FMath::Max(1, Amount);
	}

	Super::BeginPlay();
}

void ACurrencyPickup::InitializeSpawnedItem(const FInventoryItem& InItem)
{
	Super::InitializeSpawnedItem(InItem);

	// The number lives in two places for money: here, where a designer types it, and in the item.
	// BeginPlay treats this one as the truth, so a spawned pile has to be told it as well.
	Amount = FMath::Max(1, InItem.Count);
}

FInventoryItem ACurrencyPickup::MakeItemFor(const UInventoryComponent& Inventory) const
{
	FInventoryItem Offered = Super::MakeItemFor(Inventory);

	// The stack size belongs to the grid, not to the pile on the floor: one number in one place,
	// and the meta can move it without touching every pickup in every level.
	Offered.StackMax = FMath::Max(1, Inventory.GetCurrencyStackSize());
	return Offered;
}
