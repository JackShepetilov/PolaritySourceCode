// LootSheet.cpp

#include "Variant_Shooter/Map/LootSheet.h"

#include "Variant_Shooter/Map/LootGeneratorSubsystem.h"
#include "Variant_Shooter/Map/RunDirectorSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ALootSheet::ALootSheet()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mat = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mat"));
	SetRootComponent(Mat);

	// A plane, because a sheet is a plane. A Blueprint subclass swaps in the real art; what matters
	// here is that the default is flat and lies on the ground rather than being an invisible nothing
	// a designer cannot see while placing it.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneFinder.Succeeded())
	{
		Mat->SetStaticMesh(PlaneFinder.Object);
	}
	Mat->SetRelativeScale3D(FVector(2.0f, 2.0f, 1.0f));

	// Walked over, never bumped into. A mat that blocks a pawn turns a stash into a step, and one
	// that blocks a bullet makes cover out of a bedsheet.
	Mat->SetCollisionProfileName(TEXT("NoCollision"));
	Mat->SetGenerateOverlapEvents(false);
}

void ALootSheet::SetQuality(float InQuality, float InSpread)
{
	Quality = FMath::Clamp(InQuality, 0.0f, 1.0f);
	Spread = FMath::Clamp(InSpread, 0.0f, 1.0f);
}

void ALootSheet::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	LayOutItems();

	if (MoneyStacks > 0)
	{
		if (URunDirectorSubsystem* Director = URunDirectorSubsystem::GetRunDirector(this))
		{
			Director->ReportMoneyStacks(MoneyStacks);
		}
	}
}

void ALootSheet::LayOutItems()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Two ways in, and the override is the rare one. See the comment on Items: a sheet that says
	// what is on it is a special place, and a level made of special places has no generator in it.
	TArray<FLootRoll> Rolls;

	if (!Items.IsEmpty())
	{
		for (const FPoiLootEntry& Entry : Items)
		{
			FLootRoll Roll;
			Roll.Entry = Entry;
			Roll.Value = Entry.Value;
			Roll.Count = FMath::Max(1, Entry.CountMin);
			Roll.AmountScale = Entry.AmountScaleMin;
			Rolls.Add(Roll);
		}
	}
	else if (ULootGeneratorSubsystem* Generator = ULootGeneratorSubsystem::GetLootGenerator(this))
	{
		Generator->RollSheet(Quality, Spread, Rolls);
	}

	if (Rolls.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LOOT] Sheet %s rolled nothing (quality %.2f). "
			"Either the registry is not configured or its pools are empty."), *GetName(), Quality);
		return;
	}

	// Counted before placing, so the spiral below knows how full the sheet is and how tightly to
	// pack it.
	int32 SlotTotal = 0;
	for (const FLootRoll& Roll : Rolls)
	{
		if (Roll.Entry.PickupClass)
		{
			SlotTotal += Roll.Count;
		}
	}

	if (SlotTotal <= 0)
	{
		return;
	}

	int32 SlotIndex = 0;
	for (const FLootRoll& Roll : Rolls)
	{
		if (!Roll.Entry.PickupClass)
		{
			continue;
		}

		BestValue = FMath::Max(BestValue, Roll.Value);

		for (int32 i = 0; i < Roll.Count; ++i)
		{
			if (AActor* Placed = PlaceOne(Roll.Entry, Roll.AmountScale, SlotIndex, SlotTotal))
			{
				LaidOut.Add(Placed);
				if (Roll.Entry.IsMoney())
				{
					++MoneyStacks;
				}
			}
			++SlotIndex;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Loot sheet %s: quality %.2f -> %d items laid out, "
		"best value %.2f, %d of them money%s"),
		*GetName(), Quality, LaidOut.Num(), BestValue, MoneyStacks,
		Items.IsEmpty() ? TEXT("") : TEXT(" (hand-written override)"));
}

AActor* ALootSheet::PlaceOne(const FPoiLootEntry& Entry, float AmountScale, int32 SlotIndex, int32 SlotTotal)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// Golden-angle spiral: successive items land as far from each other as a disc allows, so a full
	// sheet reads as a laid-out set rather than a heap, and a nearly empty one still uses the
	// middle. A grid would do neither, and pure random would clump.
	const float Angle = SlotIndex * 2.39996323f;
	const float Radius = LayoutExtent * FMath::Sqrt((SlotIndex + 0.5f) / FMath::Max(1, SlotTotal));
	const FVector Local(
		Radius * FMath::Cos(Angle) + FMath::FRandRange(-6.0f, 6.0f),
		Radius * FMath::Sin(Angle) + FMath::FRandRange(-6.0f, 6.0f),
		DropHeight);

	const FTransform PlaceAt(FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f),
		GetActorTransform().TransformPosition(Local));

	// Deferred, because every pickup in this project reads its own payload in BeginPlay: rounds,
	// money, heal amount, armour. A scale applied afterwards is a number nobody looks at.
	AActor* Item = World->SpawnActorDeferred<AActor>(
		Entry.PickupClass, PlaceAt, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Item)
	{
		return nullptr;
	}

	PolarityLoot::ApplyAmountScale(Item, AmountScale);

	Item->FinishSpawning(PlaceAt);

	// Dropped from just above the mat and left to settle. Nothing is placed AT a height, which is
	// what used to leave pickups hanging in the air over uneven ground.
	return Item;
}
