// GrenadePickup.cpp

#include "Variant_Shooter/Pickups/GrenadePickup.h"

#include "Variant_Shooter/Grenades/GrenadeDefinition.h"

AGrenadePickup::AGrenadePickup()
{
	// Placed in a level it stays where it was put. Thrown out of the bag, the throw turns physics on.
	bStartSimulatingPhysics = false;
}

void AGrenadePickup::BeginPlay()
{
	// Before Super, as the medkit does it: the base class publishes Item to the clients as it comes up.
	if (HasAuthority())
	{
		RebuildItem();
	}

	Super::BeginPlay();
}

void AGrenadePickup::InitializeSpawnedItem(const FInventoryItem& InItem)
{
	Super::InitializeSpawnedItem(InItem);

	Type = Cast<UGrenadeDefinition>(InItem.Payload);
	Count = FMath::Max(1, InItem.Count);
}

void AGrenadePickup::RebuildItem()
{
	Item.Kind = EInventorySlotKind::Grenade;
	Item.Payload = Type;
	Item.StackMax = Type ? FMath::Max(1, Type->MaxPerCell) : 1;
	Item.Count = FMath::Max(1, Count);

	if (!Type)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRENADE_DEBUG] %s has no Type set"), *GetName());
	}
}
