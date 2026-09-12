// LootAnchor.cpp

#include "Variant_Shooter/Map/LootAnchor.h"

#include "Variant_Shooter/Map/LootSheet.h"
#include "Variant_Shooter/Map/PoiActor.h"
#include "Variant_Shooter/Map/RunDirectorSubsystem.h"

#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ALootAnchor::ALootAnchor()
{
	PrimaryActorTick.bCanEverTick = false;

	AnchorGizmo = CreateDefaultSubobject<USphereComponent>(TEXT("AnchorGizmo"));
	SetRootComponent(AnchorGizmo);

	AnchorGizmo->SetSphereRadius(100.0f);
	AnchorGizmo->SetCollisionProfileName(TEXT("NoCollision"));
	AnchorGizmo->SetGenerateOverlapEvents(false);
	AnchorGizmo->SetHiddenInGame(true);
	AnchorGizmo->ShapeColor = FColor(90, 220, 255);
}

FName ALootAnchor::GetAnchorKey() const
{
	return AnchorTag.IsNone() ? FName(*GetName()) : AnchorTag;
}

void ALootAnchor::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	RollOnce();
}

void ALootAnchor::RollOnce()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	URunDirectorSubsystem* Director = URunDirectorSubsystem::GetRunDirector(this);
	if (!Director || !Director->TryClaimOnce(GetAnchorKey()))
	{
		// This anchor has already had its turn this run. The sublevel has been here before.
		return;
	}

	if (FMath::FRand() > Chance)
	{
		UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Loot anchor %s: empty (chance %.2f)"),
			*GetAnchorKey().ToString(), Chance);
		return;
	}

	const TSubclassOf<ALootSheet> Chosen = PickSheetClass();
	if (!Chosen)
	{
		return;
	}

	// What the place is worth. An anchor with no point around it is a stash in the middle of
	// nowhere, and it uses the sheet's own defaults rather than refusing to appear.
	const APoiActor* Poi = ResolvePoi();

	// The sheet lies where the anchor stands, keeping the anchor's yaw so a designer can turn a
	// stash to face the door somebody comes through.
	//
	// Deferred, because the sheet rolls its whole contents in BeginPlay: a quality handed over
	// afterwards is a number that arrives once the crate is already full.
	const FTransform SheetAt(GetActorRotation(), GetActorLocation());
	ALootSheet* Sheet = World->SpawnActorDeferred<ALootSheet>(
		Chosen, SheetAt, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (Sheet)
	{
		if (Poi)
		{
			Sheet->SetQuality(Poi->LootQuality, Poi->LootSpread);
		}
		Sheet->FinishSpawning(SheetAt);
	}

	SpawnedSheet = Sheet;

	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Loot anchor %s: %s (point %s, quality %.2f)"),
		*GetAnchorKey().ToString(),
		Sheet ? *Sheet->GetName() : TEXT("spawn failed"),
		Poi ? *Poi->PoiTag.ToString() : TEXT("none"),
		Poi ? Poi->LootQuality : -1.0f);
}

APoiActor* ALootAnchor::ResolvePoi() const
{
	if (OwningPoi)
	{
		return OwningPoi;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// The nearest point that actually REACHES this anchor. Nearest-of-all would hand a stash
	// standing between two points to whichever centre happened to be closer, including one whose
	// influence stops short of it; containment is the same rule the point uses for everything else.
	APoiActor* Best = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	const FVector Here = GetActorLocation();
	for (TActorIterator<APoiActor> It(World); It; ++It)
	{
		APoiActor* Poi = *It;
		if (!Poi)
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared(Here, Poi->GetActorLocation());
		const float Reach = Poi->InfluenceRadius;
		if (DistanceSq <= Reach * Reach && DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = Poi;
		}
	}

	return Best;
}

TSubclassOf<ALootSheet> ALootAnchor::PickSheetClass() const
{
	float TotalWeight = 0.0f;
	for (const FLootSheetOption& Option : Sheets)
	{
		if (Option.SheetClass)
		{
			TotalWeight += FMath::Max(0.0f, Option.Weight);
		}
	}

	if (TotalWeight <= 0.0f)
	{
		return nullptr;
	}

	float Roll = FMath::FRandRange(0.0f, TotalWeight);
	for (const FLootSheetOption& Option : Sheets)
	{
		if (!Option.SheetClass)
		{
			continue;
		}

		Roll -= FMath::Max(0.0f, Option.Weight);
		if (Roll <= 0.0f)
		{
			return Option.SheetClass;
		}
	}

	return nullptr;
}
