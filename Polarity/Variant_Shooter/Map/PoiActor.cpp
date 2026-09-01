// PoiActor.cpp

#include "Variant_Shooter/Map/PoiActor.h"

#include "Variant_Shooter/Map/RunDirectorSubsystem.h"
#include "Variant_Shooter/Map/Banner.h"
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

	if (Banner)
	{
		Banner->SetOwningPoi(this);
	}

	SpawnGarrisonOnce();

	// No banner, no ceremony: a plain point just has its loot lying there. A point WITH a banner
	// holds it back until somebody breaks the thing, which is the whole mechanic.
	if (!Banner)
	{
		SpawnLootOnce(GetActorLocation(), InfluenceRadius);
	}
}

bool APoiActor::IsBannerBroken() const
{
	return Banner && Banner->bBroken;
}

void APoiActor::NotifyBannerBroken(ABannerActor* BrokenBanner, AActor* Breaker)
{
	if (!HasAuthority() || !BrokenBanner)
	{
		return;
	}

	if (URunDirectorSubsystem* Director = GetDirector())
	{
		Director->NotifyBannerBroken(PoiTag, Breaker);
	}

	// A headquarters answers this itself by dropping to its weakened sorties (see AFactionHq).
	// Everywhere else the answer is the prize, and the prize is the loot.
	if (PoiRole != EPoiRole::Headquarters)
	{
		SpawnLootOnce(BrokenBanner->GetLootBurstOrigin(), BannerLootRadius, BannerLootImpulse);
	}
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

void APoiActor::SpawnLootOnce(const FVector& Origin, float Radius, float Impulse)
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

	int32 Placed = 0;

	for (const FPoiLootEntry& Entry : Loot)
	{
		if (!Entry.PickupClass)
		{
			continue;
		}

		// The entry can still ask for its own spread; otherwise it uses whatever the caller chose,
		// which is the whole point when a banner drops a pile at its feet rather than over an acre.
		const float Spread = Entry.ScatterRadius > 0.0f ? Entry.ScatterRadius : Radius;

		for (int32 i = 0; i < Entry.Count; ++i)
		{
			const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
			const FVector Outward(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

			FVector SpawnAt;
			if (Impulse > 0.0f)
			{
				// Thrown, not placed. Everything starts inside the banner and physics decides where
				// it ends up, which is the difference between loot appearing and loot coming out.
				SpawnAt = Origin + Outward * FMath::FRandRange(0.0f, 40.0f);
			}
			else
			{
				const float Distance = Spread * FMath::Sqrt(FMath::FRand());
				const FVector Flat = Origin + Outward * Distance;

				// Laid down: find what is under that spot. A pickup floating two metres up is the
				// same bug as one buried in the floor, and both are invisible until somebody walks
				// past.
				FHitResult Hit;
				FCollisionQueryParams Params(TEXT("PoiLoot"), false, this);
				if (!World->LineTraceSingleByChannel(Hit, Flat + FVector(0.0f, 0.0f, 1000.0f),
					Flat - FVector(0.0f, 0.0f, 3000.0f), ECC_WorldStatic, Params))
				{
					continue;
				}
				SpawnAt = Hit.ImpactPoint + FVector(0.0f, 0.0f, 20.0f);
			}

			// Spawned into the persistent world rather than into this point's sublevel, so a piece
			// of loot does not vanish when the player walks far enough away from where it lies.
			AInventoryPickup* Dropped = World->SpawnActor<AInventoryPickup>(
				Entry.PickupClass, SpawnAt, FRotator::ZeroRotator, SpawnParams);
			if (!Dropped)
			{
				continue;
			}

			++Placed;

			if (Impulse > 0.0f && Dropped->Mesh && Dropped->Mesh->IsSimulatingPhysics())
			{
				// Up and out, with the spread built into the direction rather than into a radius:
				// how far a piece travels is then a consequence of the throw, and the pile lands
				// looking scattered instead of arranged.
				const FVector Launch = (Outward * FMath::FRandRange(0.5f, 1.0f)
					+ FVector(0.0f, 0.0f, FMath::FRandRange(1.0f, 1.6f))).GetSafeNormal();

				Dropped->Mesh->AddImpulse(Launch * Impulse * FMath::FRandRange(0.7f, 1.3f),
					NAME_None, /*bVelChange=*/ true);
				Dropped->Mesh->AddAngularImpulseInDegrees(
					FMath::VRand() * Impulse, NAME_None, /*bVelChange=*/ true);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] POI %s loot: %d pickups placed, %d of them money"),
		*PoiTag.ToString(), Placed, MoneyStacks);
}
