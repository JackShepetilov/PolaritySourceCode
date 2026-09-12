// InventoryPickup.cpp

#include "Variant_Shooter/Pickups/InventoryPickup.h"

#include "ChargeAnimationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EMF_FieldComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogInventoryPickup, Log, All);

AInventoryPickup::AInventoryPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	// Dropped by whoever died or by a player emptying a cell, and both of those happen on the
	// server. Without replication a client's world has no pickups in it at all: none to see, none
	// to scan for, none to grab.
	bReplicates = true;
	SetReplicateMovement(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetSimulatePhysics(true);
	Mesh->SetCollisionProfileName(FName("PhysicsActor"));
	// Not a thing the player bumps into: it is taken at range, and a pile of money that blocks a
	// doorway would be a worse problem than any it solves.
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Mesh->SetGenerateOverlapEvents(true);

	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("FieldComponent"));
}

void AInventoryPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AInventoryPickup, Item);
	DOREPLIFETIME(AInventoryPickup, bIsBeingPulled);
	DOREPLIFETIME(AInventoryPickup, bPullComplete);
	DOREPLIFETIME(AInventoryPickup, ReplicatedCharge);
}

void AInventoryPickup::BeginPlay()
{
	Super::BeginPlay();

	// Only the authority simulates; everyone else is shown where it landed. A pickup that is a
	// place rather than a dropped thing does not simulate anywhere.
	if (Mesh)
	{
		Mesh->SetSimulatePhysics(bStartSimulatingPhysics && HasAuthority());
	}

	if (HasAuthority())
	{
		// A pickup dragged into the level by hand has whatever charge its field component was
		// built with, which is nothing. Zero charge is zero reach, so it would sit there
		// unyankable with no clue as to why.
		if (FMath::IsNearlyZero(GetCharge()) && DefaultCharge > 0.0f)
		{
			SetCharge(DefaultCharge);
		}
		ReplicatedCharge = GetCharge();
	}

	BP_OnItemChanged();
}

void AInventoryPickup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsBeingPulled)
	{
		UpdatePull(DeltaTime);
	}
}

void AInventoryPickup::PostNetReceivePhysicState()
{
	if (Mesh && !Mesh->IsSimulatingPhysics())
	{
		const FRepMovement& RepMove = GetReplicatedMovement();
		SetActorLocationAndRotation(
			FRepMovement::RebaseOntoLocalOrigin(RepMove.Location, this), RepMove.Rotation);
		return;
	}

	Super::PostNetReceivePhysicState();
}

void AInventoryPickup::OnRep_Item()
{
	BP_OnItemChanged();
}

void AInventoryPickup::OnRep_DropCharge()
{
	// Through the normal setter, so anything hanging off charge behaves as it does on the server.
	SetCharge(ReplicatedCharge);
}

// ==================== Charge ====================

float AInventoryPickup::GetCharge() const
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		return Desc.PointChargeParams.Charge;
	}
	return 0.0f;
}

void AInventoryPickup::SetCharge(float NewCharge)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = NewCharge;
		FieldComponent->SetSourceDescription(Desc);
	}

	if (HasAuthority())
	{
		ReplicatedCharge = NewCharge;
	}
}

float AInventoryPickup::CalculateCaptureRange() const
{
	return UChargeAnimationComponent::GetCaptureRangeFor(this, FMath::Abs(GetCharge()));
}

// ==================== Pull ====================

bool AInventoryPickup::TryStartPullForClient(AShooterCharacter* Requester)
{
	if (!Requester || bIsBeingPulled || bPullComplete || !bCanBeCaptured)
	{
		return false;
	}

	StartPull(Requester);
	return bIsBeingPulled;
}

void AInventoryPickup::StartPull(AShooterCharacter* InPullingPlayer)
{
	if (!InPullingPlayer || bIsBeingPulled || bPullComplete || !bCanBeCaptured)
	{
		return;
	}

	bIsBeingPulled = true;
	PullElapsed = 0.0f;
	PullingCharacter = InPullingPlayer;
	PullStartLocation = GetActorLocation();
	PullStartRotation = GetActorRotation();

	if (Mesh)
	{
		Mesh->SetSimulatePhysics(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Stop exerting EMF force while flying. Same trap as the weapon drop: a live charge being
	// dragged toward the player pulls the player toward where it started.
	if (FieldComponent)
	{
		FieldComponent->UnregisterFromRegistry();
	}
}

void AInventoryPickup::UpdatePull(float DeltaTime)
{
	if (!PullingCharacter.IsValid())
	{
		// Puller gone. Back on the floor rather than frozen in mid-air.
		bIsBeingPulled = false;
		if (Mesh)
		{
			Mesh->SetSimulatePhysics(HasAuthority());
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
		if (FieldComponent)
		{
			FieldComponent->RegisterWithRegistry();
		}
		return;
	}

	PullElapsed += DeltaTime;
	const float Alpha = FMath::Clamp(PullElapsed / PullDuration, 0.0f, 1.0f);
	const float CurvedAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);

	// The camera of whoever is actually pulling, never player zero: the pull runs on the server, so
	// asking for player zero flies every client's pickup at the host's face.
	FVector CameraLoc;
	FRotator CameraRot;
	if (const APlayerController* PullerPC = Cast<APlayerController>(PullingCharacter->GetController());
		PullerPC && PullerPC->PlayerCameraManager)
	{
		CameraLoc = PullerPC->PlayerCameraManager->GetCameraLocation();
		CameraRot = PullerPC->PlayerCameraManager->GetCameraRotation();
	}
	else
	{
		PullingCharacter->GetActorEyesViewPoint(CameraLoc, CameraRot);
	}

	const FVector WorldTarget = CameraLoc + CameraRot.RotateVector(PullTargetOffset);
	SetActorLocation(FMath::Lerp(PullStartLocation, WorldTarget, CurvedAlpha));
	SetActorRotation(FMath::Lerp(PullStartRotation, CameraRot, CurvedAlpha));

	if (Alpha >= 1.0f)
	{
		CompletePull();
	}
}

void AInventoryPickup::CompletePull()
{
	bIsBeingPulled = false;

	AShooterCharacter* Player = PullingCharacter.Get();
	PullingCharacter = nullptr;

	if (!HasAuthority())
	{
		// Nothing to do here without authority: the grid is the server's, and this branch only
		// exists because a client can be running the flight visually for its own pickup.
		return;
	}

	UInventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
	if (!Inventory)
	{
		UE_LOG(LogInventoryPickup, Warning, TEXT("[INV_PICKUP] %s arrived at a character with no inventory - put back"),
			*GetName());
		ReturnToWorld(Player, Item.Count);
		return;
	}

	FInventoryItem Offered = MakeItemFor(*Inventory);
	// The cell is going to remember what it was, so that throwing it away later puts THIS actor
	// back on the floor rather than a generic stand-in holding a number.
	Offered.PickupClass = GetClass();

	if (!Offered.IsValid())
	{
		UE_LOG(LogInventoryPickup, Warning, TEXT("[INV_PICKUP] %s holds nothing valid (kind=%d count=%d) - destroyed"),
			*GetName(), static_cast<int32>(Offered.Kind), Offered.Count);
		Destroy();
		return;
	}

	const int32 Left = Inventory->TryAdd(Offered);
	const int32 Taken = Offered.Count - Left;

	UE_LOG(LogInventoryPickup, Log, TEXT("[INV_PICKUP] %s offered %d, %d taken, %d left over"),
		*GetName(), Offered.Count, Taken, Left);

	if (Taken > 0)
	{
		if (PickupSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, PickupSound, GetActorLocation());
		}
		if (PickupVFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), PickupVFX, GetActorLocation(),
				FRotator::ZeroRotator, FVector::OneVector, true, true, ENCPoolMethod::None);
		}
	}

	BP_OnCollected(Player, Taken, Left);

	if (Left <= 0)
	{
		bPullComplete = true;
		Destroy();
		return;
	}

	// The whole reason this class exists: what did not fit is still a thing in the world, in front
	// of the player who could not carry it, rather than a number that quietly evaporated.
	ReturnToWorld(Player, Left);
}

void AInventoryPickup::ReturnToWorld(AShooterCharacter* Player, int32 Remaining)
{
	if (!HasAuthority())
	{
		return;
	}

	Item.Count = FMath::Max(0, Remaining);
	if (Item.Count <= 0)
	{
		Destroy();
		return;
	}

	// Dropped in front of the player, at the height it was pulled to, so it is where they were
	// looking when it was refused.
	if (Player)
	{
		FVector EyeLoc;
		FRotator EyeRot;
		Player->GetActorEyesViewPoint(EyeLoc, EyeRot);
		static constexpr float ReturnForwardCm = 120.0f;
		SetActorLocation(EyeLoc + EyeRot.Vector() * ReturnForwardCm);
	}

	if (Mesh)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->SetSimulatePhysics(true);
	}
	if (FieldComponent)
	{
		FieldComponent->RegisterWithRegistry();
	}

	// Held channel would otherwise grab it again on the very next frame, and a refusal the player
	// never sees reads as the pickup being broken.
	bCanBeCaptured = false;
	if (RegrabDelay > 0.0f && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(RegrabTimer, this, &AInventoryPickup::AllowRegrab, RegrabDelay, false);
	}
	else
	{
		AllowRegrab();
	}

	OnRep_Item();
}

void AInventoryPickup::AllowRegrab()
{
	bCanBeCaptured = true;
}

// ==================== Item ====================

void AInventoryPickup::InitializeSpawnedItem(const FInventoryItem& InItem)
{
	Item = InItem;
}

FInventoryItem AInventoryPickup::MakeItemFor(const UInventoryComponent& Inventory) const
{
	// The base offers exactly what it was built with. Unused here on purpose: only children whose
	// numbers come from the player's own grid need to look at it.
	(void)Inventory;
	return Item;
}

AInventoryPickup* AInventoryPickup::SpawnForItem(const UObject* WorldContextObject,
	TSubclassOf<AInventoryPickup> PickupClass, const FTransform& Where,
	const FInventoryItem& InItem, float InCharge)
{
	if (!PickupClass || !InItem.IsValid())
	{
		return nullptr;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Deferred: Item has to be in place before BeginPlay, or the pickup spends its first frame
	// advertising whatever the class defaults hold.
	AInventoryPickup* Pickup = World->SpawnActorDeferred<AInventoryPickup>(
		PickupClass, Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Pickup)
	{
		return nullptr;
	}

	// Through the virtual, not by assigning Item: a child that keeps its own authored number for
	// the same quantity (ACurrencyPickup::Amount) has to be told, or its BeginPlay writes that
	// number straight back over the one being handed to it.
	Pickup->InitializeSpawnedItem(InItem);

	// Thrown, not placed, and the two are genuinely different things.
	//
	// bStartSimulatingPhysics describes how a pickup DRAGGED INTO A LEVEL starts: an ammo point or
	// an attachment on a shelf is a PLACE, and a place has to still be there next lap instead of at
	// the bottom of the nearest slope. Nothing spawned here is a place. It came out of a bag half a
	// second ago and was put in mid air in front of the player's eyes, so it has to fall.
	//
	// Set before FinishSpawning because BeginPlay is what reads it, and this is a deferred spawn:
	// after FinishSpawning it would be too late and the pile would hang in the air.
	Pickup->bStartSimulatingPhysics = true;

	if (InCharge != 0.0f)
	{
		Pickup->DefaultCharge = FMath::Abs(InCharge);
	}
	Pickup->FinishSpawning(Where);

	return Pickup;
}
