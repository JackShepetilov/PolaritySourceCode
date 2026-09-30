// SiegeMechBay.cpp

#include "SiegeMechBay.h"

#include "Components/BoxComponent.h"
#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "MechPartDefinition.h"
#include "Net/UnrealNetwork.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"

ASiegeMechBay::ASiegeMechBay()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	RootComponent = Trigger;
	Trigger->SetBoxExtent(FVector(500.0f, 500.0f, 300.0f));
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->SetCanEverAffectNavigation(false);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ASiegeMechBay::OnTriggerOverlap);
}

void ASiegeMechBay::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASiegeMechBay, bAssembled);
	DOREPLIFETIME(ASiegeMechBay, AssembledParts);
}

TArray<UMechPartDefinition*> ASiegeMechBay::GetAssembledParts() const
{
	TArray<UMechPartDefinition*> Out;
	for (const TObjectPtr<UMechPartDefinition>& PartPtr : AssembledParts)
	{
		Out.Add(PartPtr.Get());
	}
	return Out;
}

void ASiegeMechBay::OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (HasAuthority() && CoopPlayers::IsPlayer(OtherActor))
	{
		TryAssemble();
	}
}

bool ASiegeMechBay::TryAssemble()
{
	if (!HasAuthority())
	{
		return bAssembled;
	}
	if (bAssembled)
	{
		return true;
	}

	// The whole team's bags: whoever carries a part, the mech is the team's.
	struct FHeldPart
	{
		UInventoryComponent* Inventory = nullptr;
		int32 Cell = INDEX_NONE;
		UMechPartDefinition* Part = nullptr;
	};
	FHeldPart Best[static_cast<int32>(EMechPartSlot::Count)];

	TArray<APawn*> Players;
	CoopPlayers::GetAll(GetWorld(), Players);
	for (APawn* const Player : Players)
	{
		AShooterCharacter* const Character = Cast<AShooterCharacter>(Player);
		UInventoryComponent* const Inventory = Character ? Character->GetInventoryComponent() : nullptr;
		if (!Inventory)
		{
			continue;
		}
		const TArray<FInventorySlot>& Cells = Inventory->GetSlots();
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			UMechPartDefinition* const Part = Cells[Cell].Kind == EInventorySlotKind::MechPart
				? Cast<UMechPartDefinition>(Cells[Cell].Payload) : nullptr;
			if (!Part || Part->Slot >= EMechPartSlot::Count)
			{
				continue;
			}
			FHeldPart& Held = Best[static_cast<int32>(Part->Slot)];
			if (!Held.Part || Part->Rarity > Held.Part->Rarity)
			{
				Held.Inventory = Inventory;
				Held.Cell = Cell;
				Held.Part = Part;
			}
		}
	}

	TArray<FString> Missing;
	for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(EMechPartSlot::Count); ++SlotIndex)
	{
		if (!Best[SlotIndex].Part)
		{
			Missing.Add(UMechPartDefinition::SlotName(static_cast<EMechPartSlot>(SlotIndex)));
		}
	}
	if (Missing.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] %s: no mech yet, missing %s"), *GetName(), *FString::Join(Missing, TEXT(", ")));
		BP_OnAssemblyRefused(Missing);
		return false;
	}

	AssembledParts.Reset();
	for (const FHeldPart& Held : Best)
	{
		// ClearSlot empties the cell in place, so the other cell indices stay good.
		Held.Inventory->ClearSlot(Held.Cell);
		AssembledParts.Add(Held.Part);
		UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] %s: %s goes into the mech (from %s)"),
			*GetName(), *Held.Part->GetName(), *GetNameSafe(Held.Inventory->GetOwner()));
	}
	bAssembled = true;
	UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] %s: MECH ASSEMBLED, end of the demo"), *GetName());
	OnRep_Assembled();
	return true;
}

void ASiegeMechBay::OnRep_Assembled()
{
	// Both properties replicate in the same bunch as a rule, but the order is not guaranteed; the event
	// fires once, and the parts are read from the array at that moment.
	if (!bAssembled || bAnnounced)
	{
		return;
	}
	bAnnounced = true;
	const TArray<UMechPartDefinition*> Parts = GetAssembledParts();
	OnMechAssembled.Broadcast(Parts);
	BP_OnMechAssembled(Parts);
}

namespace
{
	FAutoConsoleCommandWithWorldAndArgs CmdMechAssemble(
		TEXT("polarity.mech.assemble"),
		TEXT("Try to assemble the mech at the barn now, as if a player walked in."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			for (TActorIterator<ASiegeMechBay> It(World); It; ++It)
			{
				It->TryAssemble();
			}
		}));
}
