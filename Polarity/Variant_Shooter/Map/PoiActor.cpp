// PoiActor.cpp

#include "Variant_Shooter/Map/PoiActor.h"

#include "Variant_Shooter/Map/RunDirectorSubsystem.h"
#include "Variant_Shooter/Map/Banner.h"
#include "Variant_Shooter/AI/SquadSpawn/SquadSpawnSubsystem.h"
#include "Variant_Shooter/AI/SquadSpawn/SquadLoadout.h"
#include "Variant_Shooter/AI/ShooterNPC.h"

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
}

void APoiActor::SetLootQuality(float InQuality, float InSpread)
{
	LootQuality = FMath::Clamp(InQuality, 0.0f, 1.0f);
	LootSpread = FMath::Clamp(InSpread, 0.0f, 1.0f);
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
	//
	// An ordinary point does NOT answer it yet. Breaking its banner used to spill the point's loot;
	// loot moved to sheets on anchors [author, 2026-09-02] and nothing was put in its place, so on
	// every role but Headquarters the break is currently recorded and nothing else.
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

	// The tag matters as much as the bodies: a garrison that does not know which place it is FOR
	// can never be asked how many are wanted there, and so can never give its surplus up.
	const int32 Spawned = Squads->SpawnSquadMembers(GetActorLocation(), GarrisonScatterRadius,
		GarrisonLoadout, nullptr, 0, PoiTag);
	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] POI %s garrison: %d members from %s"),
		*PoiTag.ToString(), Spawned, *GarrisonLoadout->GetName());
}
