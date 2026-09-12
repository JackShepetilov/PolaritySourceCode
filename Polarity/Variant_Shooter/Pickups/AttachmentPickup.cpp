// AttachmentPickup.cpp

#include "Variant_Shooter/Pickups/AttachmentPickup.h"

#include "Components/StaticMeshComponent.h"
#include "Variant_Shooter/Weapons/WeaponAttachmentDefinition.h"

DEFINE_LOG_CATEGORY_STATIC(LogAttachmentPickup, Log, All);

AAttachmentPickup::AAttachmentPickup()
{
	// An attachment placed in a level is a PLACE the player walks to, the same as an ammo point, so
	// it stays where it was put rather than rolling downhill. Throwing one out of the bag turns
	// physics back on for the throw regardless.
	bStartSimulatingPhysics = false;
}

void AAttachmentPickup::BeginPlay()
{
	// Before Super, exactly as the ammo pile does it: the base class publishes Item to the clients
	// as it comes up, so it has to be right by then.
	if (HasAuthority())
	{
		RebuildItem();
	}

	Super::BeginPlay();

	ApplyAttachmentLook();
}

void AAttachmentPickup::InitializeSpawnedItem(const FInventoryItem& InItem)
{
	Super::InitializeSpawnedItem(InItem);

	// A pickup made out of a thrown-away cell. The cell points at the same definition asset, so a
	// dropped scope lands as that scope rather than as whatever this Blueprint was authored with.
	Attachment = Cast<UWeaponAttachmentDefinition>(InItem.Payload);
}

void AAttachmentPickup::ApplyAttachmentLook()
{
	if (!Mesh)
	{
		return;
	}

	// The attachment's own mesh, so the thing on the floor IS the thing that goes on the gun. A
	// definition with no mesh leaves whatever the Blueprint set, which is the sensible fallback for
	// a part that has no art yet.
	if (Attachment && Attachment->Mesh)
	{
		Mesh->SetStaticMesh(Attachment->Mesh);
		Mesh->SetRelativeScale3D(Attachment->MeshScale);
	}

	Mesh->SetVisibility(true);
}

void AAttachmentPickup::RebuildItem()
{
	Item.Kind = EInventorySlotKind::Attachment;
	Item.Payload = Attachment;

	// Attachments never stack. Two of the same scope are two things to carry and two decisions to
	// make about them, so one is one cell.
	Item.Count = 1;
	Item.StackMax = 1;

	if (!Attachment)
	{
		UE_LOG(LogAttachmentPickup, Warning, TEXT("[ATTACH] %s has no Attachment set. It will be "
			"refused by the grid, because a cell with no definition is not an attachment."),
			*GetName());
	}
}
