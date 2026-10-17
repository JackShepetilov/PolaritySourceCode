// GrenadePickup.cpp

#include "Variant_Shooter/Pickups/GrenadePickup.h"

#include "Variant_Shooter/Grenades/GrenadeDefinition.h"
#include "Components/StaticMeshComponent.h"

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

	// A pile with no mesh of its own (dropped to make room for another kind) shows the type's. Before
	// Super, which turns the physics on: a body needs the mesh to simulate. On a client Item has
	// arrived with the actor, before BeginPlay.
	const UGrenadeDefinition* const Def = Type ? Type.Get() : Cast<UGrenadeDefinition>(Item.Payload);
	if (Mesh && !Mesh->GetStaticMesh() && Def && Def->PickupMesh)
	{
		Mesh->SetStaticMesh(Def->PickupMesh);
		Mesh->SetRelativeScale3D(Def->PickupMeshScale);
		Mesh->SetVisibility(true);
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
