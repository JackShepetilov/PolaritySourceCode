// PoiActor.cpp

#include "Variant_Shooter/Map/PoiActor.h"

#include "Variant_Shooter/Map/RunDirectorSubsystem.h"
#include "Variant_Shooter/AI/SquadSpawn/SquadSpawnSubsystem.h"
#include "Variant_Shooter/AI/SquadSpawn/SquadLoadout.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Pickups/InventoryPickup.h"

#include "AI/PolarityTeams.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

APoiActor::APoiActor()
{
	PrimaryActorTick.bCanEverTick = true;

	InfluenceGizmo = CreateDefaultSubobject<USphereComponent>(TEXT("InfluenceGizmo"));
	SetRootComponent(InfluenceGizmo);

	// Query-only and overlap-only: this sphere is how the point sees who is standing on it, and it
	// must never push a pawn or stop a bullet.
	InfluenceGizmo->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InfluenceGizmo->SetCollisionResponseToAllChannels(ECR_Ignore);
	InfluenceGizmo->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InfluenceGizmo->SetGenerateOverlapEvents(true);
	InfluenceGizmo->SetHiddenInGame(true);
	InfluenceGizmo->ShapeColor = FColor(255, 190, 60);
}

void APoiActor::BeginPlay()
{
	Super::BeginPlay();

	InfluenceGizmo->SetSphereRadius(InfluenceRadius, false);

	if (!HasAuthority())
	{
		return;
	}

	if (URunDirectorSubsystem* Director = GetDirector())
	{
		Director->RegisterPoi(this);
	}

	SpawnGarrisonOnce();
	SpawnLootOnce();
}

void APoiActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HasAuthority())
	{
		if (URunDirectorSubsystem* Director = GetDirector())
		{
			Director->UnregisterPoi(this);
		}
	}

	Super::EndPlay(Reason);
}

void APoiActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

	PresenceTimer += DeltaSeconds;
	if (PresenceTimer < PresenceIntervalSeconds)
	{
		return;
	}

	ReportPresence(PresenceTimer);
	PresenceTimer = 0.0f;
}

URunDirectorSubsystem* APoiActor::GetDirector() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<URunDirectorSubsystem>() : nullptr;
}

bool APoiActor::GetWarState(FPoiWarState& OutState) const
{
	const URunDirectorSubsystem* Director = GetDirector();
	return Director ? Director->GetPoiState(PoiTag, OutState) : false;
}

void APoiActor::ReportPresence(float DeltaSeconds)
{
	URunDirectorSubsystem* Director = GetDirector();
	if (!Director)
	{
		return;
	}

	TArray<AActor*> Overlapping;
	InfluenceGizmo->GetOverlappingActors(Overlapping, APawn::StaticClass());

	int32 Players = 0;
	int32 FactionA = 0;
	int32 FactionB = 0;

	for (const AActor* Actor : Overlapping)
	{
		// A body on the floor is not a garrison. Without this a point stays "held" by whoever lost
		// the fight, because their corpses are still inside the sphere.
		if (const AShooterNPC* NPC = Cast<AShooterNPC>(Actor))
		{
			if (NPC->IsDead())
			{
				continue;
			}
		}

		// Resolved through the actor, exactly the way the perception system resolves a stimulus
		// source, so a point and a pair of eyes never disagree about whose side somebody is on.
		switch (PolarityTeams::GetTeam(Actor))
		{
		case PolarityTeams::Players:  ++Players; break;
		case PolarityTeams::FactionA: ++FactionA; break;
		case PolarityTeams::FactionB: ++FactionB; break;
		default: break;
		}
	}

	Director->ReportPoiPresence(this, Players, FactionA, FactionB, DeltaSeconds);
}

void APoiActor::SpawnGarrisonOnce()
{
	if (!GarrisonLoadout)
	{
		return;
	}

	URunDirectorSubsystem* Director = GetDirector();
	if (!Director || !Director->TryClaimGarrisonSpawn(PoiTag))
	{
		// Already spawned earlier in this run: the sublevel has been here before.
		return;
	}

	USquadSpawnSubsystem* Squads = GetWorld() ? GetWorld()->GetSubsystem<USquadSpawnSubsystem>() : nullptr;
	if (!Squads)
	{
		return;
	}

	const int32 Spawned = Squads->SpawnSquadMembers(GetActorLocation(), GarrisonScatterRadius, GarrisonLoadout, nullptr);
	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] POI %s garrison: %d members from %s"),
		*PoiTag.ToString(), Spawned, *GarrisonLoadout->GetName());
}

void APoiActor::SpawnLootOnce()
{
	if (Loot.IsEmpty())
	{
		return;
	}

	URunDirectorSubsystem* Director = GetDirector();
	UWorld* World = GetWorld();
	if (!Director || !World)
	{
		return;
	}

	int32 MoneyStacks = 0;
	for (const FPoiLootEntry& Entry : Loot)
	{
		if (Entry.bIsMoney)
		{
			MoneyStacks += Entry.Count;
		}
	}

	if (!Director->TryClaimLootSpawn(PoiTag, MoneyStacks))
	{
		return;
	}

	const FVector Origin = GetActorLocation();
	int32 Placed = 0;

	for (const FPoiLootEntry& Entry : Loot)
	{
		if (!Entry.PickupClass)
		{
			continue;
		}

		const float Radius = Entry.ScatterRadius > 0.0f ? Entry.ScatterRadius : InfluenceRadius;

		for (int32 i = 0; i < Entry.Count; ++i)
		{
			const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
			const float Distance = Radius * FMath::Sqrt(FMath::FRand());
			const FVector Flat = Origin + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.0f);

			// Drop it on whatever is under that spot. A pickup floating two metres up is the same
			// bug as one buried in the floor, and both are invisible until somebody walks past.
			FHitResult Hit;
			const FVector TraceStart = Flat + FVector(0.0f, 0.0f, 1000.0f);
			const FVector TraceEnd = Flat - FVector(0.0f, 0.0f, 3000.0f);

			FCollisionQueryParams Params(TEXT("PoiLoot"), false, this);
			if (!World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, Params))
			{
				continue;
			}

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

			// Spawned into the persistent world rather than into this point's sublevel, so a piece
			// of loot does not vanish when the player walks far enough away from where it lies.
			if (World->SpawnActor<AInventoryPickup>(Entry.PickupClass, Hit.ImpactPoint + FVector(0.0f, 0.0f, 20.0f),
				FRotator::ZeroRotator, SpawnParams))
			{
				++Placed;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] POI %s loot: %d pickups placed, %d of them money"),
		*PoiTag.ToString(), Placed, MoneyStacks);
}
