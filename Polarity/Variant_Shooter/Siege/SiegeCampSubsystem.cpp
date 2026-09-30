// SiegeCampSubsystem.cpp

#include "SiegeCampSubsystem.h"

#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "MechPartDefinition.h"
#include "SiegeCampSite.h"
#include "TimerManager.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"

bool USiegeCampSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USiegeCampSubsystem::RegisterCamp(ASiegeCampSite* Camp)
{
	if (!Camp)
	{
		return;
	}
	Camps.AddUnique(Camp);
	if (!bDealScheduled)
	{
		bDealScheduled = true;
		// Next tick, not now: the other camps of the level have not had their BeginPlay yet. A zero
		// delay SetTimer would never fire (Build_Cpp.md).
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &USiegeCampSubsystem::AssignParts));
	}
}

void USiegeCampSubsystem::GetCamps(TArray<ASiegeCampSite*>& OutCamps) const
{
	for (const TWeakObjectPtr<ASiegeCampSite>& Weak : Camps)
	{
		if (ASiegeCampSite* const Camp = Weak.Get())
		{
			OutCamps.Add(Camp);
		}
	}
}

namespace
{
	/** A part of this slot from the camp's pool: its tier's rarity first, any rarity after that. */
	UMechPartDefinition* PickCampPart(const ASiegeCampSite* Camp, EMechPartSlot Slot)
	{
		UMechPartDefinition* AnyRarity = nullptr;
		for (UMechPartDefinition* const Candidate : Camp->PartPool)
		{
			if (!Candidate || Candidate->Slot != Slot)
			{
				continue;
			}
			if (static_cast<uint8>(Candidate->Rarity) == Camp->GetPartRarity())
			{
				return Candidate;
			}
			AnyRarity = AnyRarity ? AnyRarity : Candidate;
		}
		return AnyRarity;
	}
}

void USiegeCampSubsystem::AssignParts()
{
	TArray<ASiegeCampSite*> Live;
	GetCamps(Live);

	// By side. A camp without a side is a side of its own, so it still gets a part.
	TMap<FName, TArray<ASiegeCampSite*>> BySide;
	for (ASiegeCampSite* const Camp : Live)
	{
		BySide.FindOrAdd(Camp->ForestSide.IsNone() ? FName(*Camp->GetName()) : Camp->ForestSide).Add(Camp);
	}

	const int32 NumSlots = static_cast<int32>(EMechPartSlot::Count);
	for (TPair<FName, TArray<ASiegeCampSite*>>& Side : BySide)
	{
		TArray<ASiegeCampSite*>& SideCamps = Side.Value;

		// Every slot once, then random ones for the camps past five; then the whole list shuffled
		// against the camps, so where each slot lies changes from run to run.
		TArray<EMechPartSlot> Deck;
		for (int32 i = 0; i < SideCamps.Num(); ++i)
		{
			Deck.Add(static_cast<EMechPartSlot>(i < NumSlots ? i : FMath::RandRange(0, NumSlots - 1)));
		}
		for (int32 i = Deck.Num() - 1; i > 0; --i)
		{
			Deck.Swap(i, FMath::RandRange(0, i));
		}

		for (int32 i = 0; i < SideCamps.Num(); ++i)
		{
			ASiegeCampSite* const Camp = SideCamps[i];
			UMechPartDefinition* const Part = PickCampPart(Camp, Deck[i]);
			Camp->SetPart(Part);
			UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] deal: %s (%s, side %s) gets %s"),
				*Camp->SlotName.ToString(), *StaticEnum<ESiegeCampTier>()->GetNameStringByValue(static_cast<int64>(Camp->Tier)),
				*Side.Key.ToString(), Part ? *Part->GetName() : TEXT("NOTHING (no part of that slot in PartPool)"));
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] dealt parts to %d camps on %d sides"), Live.Num(), BySide.Num());
}

// ==================== Console ====================

namespace
{
	USiegeCampSubsystem* FindCampSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<USiegeCampSubsystem>() : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs CmdCampList(
		TEXT("polarity.camp.list"),
		TEXT("Forest camps: slot, tier, state, part."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			USiegeCampSubsystem* const Subsystem = FindCampSubsystem(World);
			if (!Subsystem)
			{
				return;
			}
			TArray<ASiegeCampSite*> Live;
			Subsystem->GetCamps(Live);
			for (const ASiegeCampSite* const Camp : Live)
			{
				UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] %s %s side=%s state=%s part=%s"),
					*Camp->SlotName.ToString(),
					*StaticEnum<ESiegeCampTier>()->GetNameStringByValue(static_cast<int64>(Camp->Tier)),
					*Camp->ForestSide.ToString(),
					*StaticEnum<ESiegeCampState>()->GetNameStringByValue(static_cast<int64>(Camp->GetState())),
					*GetNameSafe(Camp->GetPart()));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdCampWake(
		TEXT("polarity.camp.wake"),
		TEXT("Wake a forest camp now: polarity.camp.wake <SlotName> | all."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			USiegeCampSubsystem* const Subsystem = FindCampSubsystem(World);
			if (!Subsystem || Args.Num() == 0)
			{
				return;
			}
			TArray<ASiegeCampSite*> Live;
			Subsystem->GetCamps(Live);
			for (ASiegeCampSite* const Camp : Live)
			{
				if (Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase) || Camp->SlotName.ToString().Equals(Args[0], ESearchCase::IgnoreCase))
				{
					Camp->Wake();
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdCampGiveParts(
		TEXT("polarity.camp.giveparts"),
		TEXT("Put one part of every slot into each player's inventory (testing the barn)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			USiegeCampSubsystem* const Subsystem = FindCampSubsystem(World);
			if (!Subsystem)
			{
				return;
			}
			TArray<ASiegeCampSite*> Live;
			Subsystem->GetCamps(Live);
			TArray<APawn*> Players;
			CoopPlayers::GetAll(World, Players);
			for (APawn* const Player : Players)
			{
				AShooterCharacter* const Character = Cast<AShooterCharacter>(Player);
				UInventoryComponent* const Inventory = Character ? Character->GetInventoryComponent() : nullptr;
				if (!Inventory || !Inventory->GetOwner()->HasAuthority() || Live.Num() == 0)
				{
					continue;
				}
				for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(EMechPartSlot::Count); ++SlotIndex)
				{
					if (UMechPartDefinition* const Part = PickCampPart(Live[0], static_cast<EMechPartSlot>(SlotIndex)))
					{
						FInventoryItem Item;
						Item.Kind = EInventorySlotKind::MechPart;
						Item.Payload = Part;
						Inventory->TryAdd(Item);
					}
				}
				UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] giveparts: %s"), *Player->GetName());
			}
		}));
}
