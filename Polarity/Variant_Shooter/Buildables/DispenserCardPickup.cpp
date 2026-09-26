// DispenserCardPickup.cpp

#include "DispenserCardPickup.h"

#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "EMF_FieldComponent.h"
#include "DispenserSlotMachineComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Polarity/Upgrades/DispenserUpgradePool.h"
#include "Polarity/Upgrades/UpgradeDefinition.h"
#include "Variant_Shooter/Pickups/AttachmentPickup.h"
#include "Variant_Shooter/Pickups/InventoryPickup.h"
#include "Variant_Shooter/Pickups/UpgradePickup.h"
#include "Variant_Shooter/ShooterCharacter.h"

ADispenserCardPickup::ADispenserCardPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// The flight to the hand is driven on the server; the watchers follow it by movement replication.
	SetReplicatingMovement(true);

	// What the hook traces and brackets, nothing else: it blocks no one and nothing blocks through it.
	Bounds = CreateDefaultSubobject<USphereComponent>(TEXT("Bounds"));
	Bounds->InitSphereRadius(25.0f);
	Bounds->SetCollisionProfileName(TEXT("NoCollision"));
	Bounds->SetCanEverAffectNavigation(false);
	SetRootComponent(Bounds);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Bounds);
	Mesh->SetCollisionProfileName(TEXT("NoCollision"));
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetRelativeScale3D(FVector(0.3f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		Mesh->SetStaticMesh(Sphere.Object);
	}

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Bounds);
	Label->SetRelativeLocation(FVector(0.0f, 0.0f, 30.0f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(12.0f);
	// The loot card over the bracketed item names it now; the floating text only doubled it.
	Label->SetHiddenInGame(true);
}

void ADispenserCardPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADispenserCardPickup, Machine);
	DOREPLIFETIME(ADispenserCardPickup, CardIndex);
	DOREPLIFETIME(ADispenserCardPickup, LabelText);
	DOREPLIFETIME(ADispenserCardPickup, Color);
	DOREPLIFETIME(ADispenserCardPickup, bPulling);
}

void ADispenserCardPickup::InitCard(UDispenserSlotMachineComponent* InMachine, int32 InCardIndex, const FText& InLabel, const FLinearColor& InColor)
{
	Machine = InMachine;
	CardIndex = InCardIndex;
	LabelText = InLabel;
	Color = InColor;
}

void ADispenserCardPickup::BeginPlay()
{
	Super::BeginPlay();

	// The hook, its brackets and its line all find the item's middle with GetActorBounds(true), which
	// counts colliding components only: with none, the middle is the world origin and the item is
	// never under the crosshair. Query-only and ignoring every channel, the sphere still blocks and
	// overlaps nothing.
	if (Bounds)
	{
		Bounds->SetCollisionResponseToAllChannels(ECR_Ignore);
		Bounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}

	// On every machine: the owning client's brackets search this list, the server's claim check too.
	GrappleFetch::Register(this);
	ApplyLook();
	TryBuildVisual();
	UpdatePresentation();
}

void ADispenserCardPickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GrappleFetch::Unregister(this);
	if (Visual)
	{
		Visual->Destroy();
		Visual = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ADispenserCardPickup::TryBuildVisual()
{
	UWorld* const World = GetWorld();
	if (bVisualTried || !World || !Machine || !Machine->GetSpin().Cards.IsValidIndex(CardIndex))
	{
		return;
	}
	bVisualTried = true;

	// A buff has no pickup to copy: this item wears the buff's own mesh (none authored: the sphere).
	if (const UDispenserBuffDefinition* const Buff = Machine->GetCardBuff(CardIndex))
	{
		if (Mesh && Buff->Mesh)
		{
			Mesh->SetStaticMesh(Buff->Mesh);
			Mesh->SetRelativeScale3D(FVector::OneVector);
			ApplyLook();
		}
		return;
	}

	const TSubclassOf<AActor> VisualClass = Machine->GetCardVisualClass(CardIndex);
	if (!VisualClass)
	{
		return;
	}
	const FDispenserCard& Card = Machine->GetSpin().Cards[CardIndex];

	AActor* const Copy = World->SpawnActorDeferred<AActor>(VisualClass, GetActorTransform(), this, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Copy)
	{
		return;
	}
	// A local copy on every machine, never a networked one: each machine makes its own.
	Copy->SetReplicates(false);
	if (AUpgradePickup* const Upgrade = Cast<AUpgradePickup>(Copy))
	{
		// Before BeginPlay: the hologram is built from it there.
		Upgrade->UpgradeDefinition = Card.Upgrade.Definition;
	}
	if (AAttachmentPickup* const AttachmentCopy = Cast<AAttachmentPickup>(Copy))
	{
		// Before BeginPlay: it builds its item and wears the attachment's mesh there.
		AttachmentCopy->Attachment = Machine->GetCardAttachment(CardIndex);
	}
	if (AInventoryPickup* const Inventory = Cast<AInventoryPickup>(Copy))
	{
		Inventory->bStartSimulatingPhysics = false;
	}
	Copy->FinishSpawning(GetActorTransform());
	if (AAttachmentPickup* const AttachmentCopy = Cast<AAttachmentPickup>(Copy))
	{
		AttachmentCopy->ApplyAttachmentLook();
	}

	// Everything it would do on its own, off: nobody can hook it, touch it, be pulled by its charge
	// or have it fly at them; it does not fall and does not tick. It is only a look now.
	GrappleFetch::Unregister(Copy);
	Copy->SetActorEnableCollision(false);
	Copy->SetActorTickEnabled(false);
	TArray<UActorComponent*> Components;
	Copy->GetComponents(Components);
	for (UActorComponent* const Component : Components)
	{
		if (UPrimitiveComponent* const Primitive = Cast<UPrimitiveComponent>(Component))
		{
			Primitive->SetSimulatePhysics(false);
		}
		if (UEMF_FieldComponent* const Field = Cast<UEMF_FieldComponent>(Component))
		{
			Field->UnregisterFromRegistry();
		}
		if (UWidgetComponent* const Widget = Cast<UWidgetComponent>(Component))
		{
			// No hologram in the box: the loot card on the bracketed item says what it is.
			Widget->SetVisibility(false);
		}
		Component->SetComponentTickEnabled(false);
	}
	Copy->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);

	// An upgrade wears the cartridge, tinted by rarity and showing the upgrade's own icon.
	if (AUpgradePickup* const Upgrade = Cast<AUpgradePickup>(Copy))
	{
		const UDispenserUpgradePool* const Pool = Machine->GetPool();
		UStaticMesh* const Cartridge = Pool ? Pool->UpgradeCartridgeMesh.LoadSynchronous() : nullptr;
		if (Upgrade->Mesh && Cartridge)
		{
			// The Blueprint scales its own mesh to size (BP_360Pickup: x7.5 for a tiny chip); the
			// cartridge is modelled at its real size, so that scale must not carry over.
			Upgrade->Mesh->SetStaticMesh(Cartridge);
			Upgrade->Mesh->SetRelativeScale3D(FVector::OneVector);
		}
		if (Upgrade->Mesh && Upgrade->Mesh->GetMaterial(0))
		{
			if (UMaterialInstanceDynamic* const Dynamic = Upgrade->Mesh->CreateDynamicMaterialInstance(0))
			{
				Dynamic->SetVectorParameterValue(TEXT("RarityColor"), Color);
				if (Card.Upgrade.Definition && Card.Upgrade.Definition->Icon)
				{
					Dynamic->SetTextureParameterValue(TEXT("Icon"), Card.Upgrade.Definition->Icon);
				}
			}
		}
	}

	Visual = Copy;
	bWasPresented = !Machine->IsCardPresented(CardIndex);
	UpdatePresentation();
	if (Mesh)
	{
		Mesh->SetVisibility(false);
	}
}

void ADispenserCardPickup::OnRep_Look()
{
	ApplyLook();
}

void ADispenserCardPickup::ApplyLook()
{
	if (Label)
	{
		Label->SetText(LabelText);
		Label->SetTextRenderColor(Color.ToFColor(true));
	}
	if (Mesh && Mesh->GetMaterial(0))
	{
		// The engine's basic shape material takes "Color"; the 3D pass's cartridge takes "RarityColor".
		UMaterialInstanceDynamic* const Dynamic = Mesh->CreateDynamicMaterialInstance(0);
		if (Dynamic)
		{
			Dynamic->SetVectorParameterValue(TEXT("Color"), Color);
			Dynamic->SetVectorParameterValue(TEXT("RarityColor"), Color);
		}
	}
}

void ADispenserCardPickup::UpdatePresentation()
{
	const bool bPresented = Machine && Machine->IsCardPresented(CardIndex);
	if (bPresented == bWasPresented)
	{
		return;
	}
	bWasPresented = bPresented;
	if (Label)
	{
		Label->SetVisibility(bPresented);
	}
	if (Visual)
	{
		TArray<UWidgetComponent*> Widgets;
		Visual->GetComponents<UWidgetComponent>(Widgets);
		for (UWidgetComponent* const Widget : Widgets)
		{
			Widget->SetVisibility(bPresented);
		}
	}
}

bool ADispenserCardPickup::CanBeGrappleFetchedBy(const AShooterCharacter* Caster) const
{
	return Caster && Machine && !bTaken && !bPulling && !IsHidden() && Machine->IsSpinner(Caster)
		&& Machine->IsCardPresented(CardIndex);
}

bool ADispenserCardPickup::BeginGrappleFetchPull(AShooterCharacter* Caster)
{
	if (!HasAuthority() || !CanBeGrappleFetchedBy(Caster))
	{
		return false;
	}
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	bPulling = true;
	ForceNetUpdate();
	Puller = Caster;
	PullStart = GetActorLocation();
	PullElapsed = 0.0f;
	return true;
}

void ADispenserCardPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The spin this card belongs to may replicate after the card itself.
	if (!bVisualTried)
	{
		TryBuildVisual();
	}

	UpdatePresentation();
	// Cassette contents rest on the moving tray, without a floating display spin.
	if (!bPulling && (!Machine || !Machine->HasCassettes()))
	{
		if (Visual)
		{
			Visual->AddActorLocalRotation(FRotator(0.0f, 90.0f * DeltaSeconds, 0.0f));
		}
		else if (Mesh)
		{
			Mesh->AddLocalRotation(FRotator(0.0f, 90.0f * DeltaSeconds, 0.0f));
		}
	}

	if (!HasAuthority() || !bPulling)
	{
		return;
	}

	AShooterCharacter* const Target = Puller.Get();
	if (!Target)
	{
		// The puller is gone: back in the box, free again.
		bPulling = false;
		SetActorLocation(PullStart);
		if (Machine && Machine->HasCassettes() && Machine->GetMachineMesh() && Machine->BoxSockets.IsValidIndex(CardIndex))
		{
			AttachToComponent(Machine->GetMachineMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Machine->BoxSockets[CardIndex]);
		}
		ForceNetUpdate();
		return;
	}

	PullElapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(PullElapsed / FMath::Max(0.01f, PullDuration), 0.0f, 1.0f);
	SetActorLocation(FMath::Lerp(PullStart, Target->GetGrappleHandLocation(), FMath::InterpEaseIn(0.0f, 1.0f, Alpha, 2.0f)));
	if (Alpha < 1.0f)
	{
		return;
	}

	// Arrived. The machine gives the card and shuts the other boxes; the hands come back either way.
	bPulling = false;
	bTaken = true;
	if (Machine)
	{
		Machine->TakeCard(CardIndex, Target);
	}
	Target->FinishWeaponFetch(false);
	Destroy();
}
