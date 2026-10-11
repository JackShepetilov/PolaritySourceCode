// MedkitPickup.cpp

#include "Variant_Shooter/Pickups/MedkitPickup.h"

#include "Variant_Shooter/Inventory/InventoryComponent.h"

AMedkitPickup::AMedkitPickup()
{
	// Placed in a level it is a place the player walks to, so it stays where it was put. Thrown out
	// of the bag, the throw turns physics on regardless.
	bStartSimulatingPhysics = false;
}

void AMedkitPickup::BeginPlay()
{
	// Before Super, as the ammo pile does it: the base class publishes Item to the clients as it
	// comes up, so it has to be right by then.
	if (HasAuthority())
	{
		RebuildItem();
	}

	Super::BeginPlay();
}

void AMedkitPickup::InitializeSpawnedItem(const FInventoryItem& InItem)
{
	Super::InitializeSpawnedItem(InItem);

	Medkits = FMath::Max(1, InItem.Count);
}

void AMedkitPickup::RebuildItem()
{
	Item.Kind = EInventorySlotKind::Medkit;
	Item.Payload = nullptr;
	Item.StackMax = FMath::Max(1, GetDefault<UInventoryComponent>()->GetMedkitStackSize());
	Item.Count = FMath::Max(1, Medkits);
}

FInventoryItem AMedkitPickup::MakeItemFor(const UInventoryComponent& Inventory) const
{
	FInventoryItem Offered = Super::MakeItemFor(Inventory);
	Offered.StackMax = Inventory.GetMedkitStackSize();
	return Offered;
}
