// AmmoPickup.cpp

#include "Variant_Shooter/Pickups/AmmoPickup.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "NiagaraComponent.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

DEFINE_LOG_CATEGORY_STATIC(LogAmmoPickup, Log, All);

AAmmoPickup::AAmmoPickup()
{
	// Rounds fall like everything else [author, 2026-09-01]. They used to be pinned in place as "a
	// place, not a prop", which meant a pile spawned above the ground simply hung there.
	bStartSimulatingPhysics = true;

	Effect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Effect"));
	Effect->SetupAttachment(Mesh);
	Effect->SetAutoActivate(true);

	// The root mesh is INVISIBLE but not empty, and the difference is the whole bug it fixes.
	//
	// Everything you see is a mesh renderer inside the Niagara system, so the root was left with no
	// static mesh at all - and a UStaticMeshComponent without a mesh has no collision shapes, which
	// cost this pickup both of its jobs: the yank scans with OverlapMultiByObjectType and found
	// nothing to grab, and physics had no body to simulate, so a pile spawned in the air stayed in
	// the air.
	//
	// The body it gets is AmmoMesh itself, assigned in ApplyWeaponLook - the same mesh Niagara
	// draws, so what falls and what you grab is the shape you can see. This engine sphere is only
	// the fallback for a pickup with no AmmoMesh set at all, which is a misconfigured asset rather
	// than a normal state.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Mesh)
	{
		if (BodyFinder.Succeeded() && !Mesh->GetStaticMesh())
		{
			Mesh->SetStaticMesh(BodyFinder.Object);
		}
		// NOT scaled down. Effect hangs off Mesh, so shrinking the root shrank the swirl with it:
		// the art was drawn at 30% and the body was a 15 cm ball that behaved like nothing on
		// screen. Every other pickup in the project (see AAttachmentPickup) puts the real mesh on
		// the root at its own scale, and this one now does the same.
		Mesh->SetVisibility(false);
	}
}

void AAmmoPickup::BeginPlay()
{
	if (HasAuthority())
	{
		RebuildItem();
	}

	// BEFORE the base class, on purpose. Super::BeginPlay is where physics is switched on, and
	// ApplyWeaponLook is what decides the shape of the body: doing it afterwards means the pile
	// starts simulating as one thing and then silently becomes another. It is also the right order
	// for the Niagara parameters, which are read when the system activates in component BeginPlay.
	ApplyWeaponLook();

	Super::BeginPlay();
}

void AAmmoPickup::InitializeSpawnedItem(const FInventoryItem& InItem)
{
	Super::InitializeSpawnedItem(InItem);

	// A pile made from a thrown-away cell knows only how many rounds it holds; there is one kind.
	Rounds = FMath::Max(1, InItem.Count);
}

void AAmmoPickup::RebuildItem()
{
	Item.Kind = EInventorySlotKind::Ammo;
	Item.Payload = nullptr;

	// One pool, so the stack is the inventory's cell size rather than some gun's magazine. Read
	// from the class default: a pile lying in a level has no owner to ask.
	const int32 CellSize = FMath::Max(1, GetDefault<UInventoryComponent>()->GetRoundsPerAmmoCell());

	Item.StackMax = CellSize;
	// Zero rounds means "a cell's worth", which is the useful default for an ammo point placed by
	// hand: exactly one cell, no number to keep in step with anything.
	Item.Count = (Rounds > 0) ? Rounds : CellSize;
}

void AAmmoPickup::ApplyWeaponLook()
{
	if (!Effect)
	{
		return;
	}

	// One pool of rounds, so one colour and one shape, both of them this actor's own. They used to
	// be the gun's, because a pile had to say which gun it was for; nothing has to say that now.
	// The colour reaches the meshes through the particle: user parameter -> Particles.Color ->
	// M_PickupTint emissive.
	Effect->SetVariableLinearColor(ColorParameterName, AmmoColor);
	Effect->SetVariableStaticMesh(MeshParameterName, AmmoMesh);

	// The same mesh becomes the physics body, so the pile falls, rolls, rests and is grabbed as the
	// shape on screen rather than as an unrelated ball hidden inside it. Kept invisible: Niagara is
	// still the only thing that draws.
	if (Mesh && AmmoMesh)
	{
		Mesh->SetStaticMesh(AmmoMesh);
		Mesh->SetVisibility(false);
	}

	UE_LOG(LogAmmoPickup, Log, TEXT("[AMMO_PICKUP] %s: wrote '%s'=%s and '%s'=%s"),
		*GetName(), *ColorParameterName.ToString(), *AmmoColor.ToString(),
		*MeshParameterName.ToString(), *GetNameSafe(AmmoMesh));
}

FInventoryItem AAmmoPickup::MakeItemFor(const UInventoryComponent& Inventory) const
{
	FInventoryItem Offered = Super::MakeItemFor(Inventory);

	// The taker's own cell size, not the class default: this is the one place where a real
	// inventory is in hand, and it is the bag being filled that decides how much fits in a cell.
	Offered.StackMax = Inventory.GetRoundsPerAmmoCell();

	// What the pile is WORTH is the weapon in the taker's hands, not anything about the pile.
	// A pile that names its own count keeps it: a level placing "12 rounds" means twelve.
	if (Rounds <= 0)
	{
		if (const AShooterCharacter* Taker = Cast<AShooterCharacter>(Inventory.GetOwner()))
		{
			if (const AShooterWeapon* InHand = Taker->GetCurrentWeapon())
			{
				Offered.Count = InHand->GetRoundsFromPickup();
			}
		}
	}

	return Offered;
}
