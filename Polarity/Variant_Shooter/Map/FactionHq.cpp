// FactionHq.cpp

#include "Variant_Shooter/Map/FactionHq.h"

#include "Variant_Shooter/Map/RunDirectorSubsystem.h"
#include "Variant_Shooter/Map/Banner.h"
#include "Variant_Shooter/AI/SquadSpawn/SquadSpawnSubsystem.h"
#include "Variant_Shooter/AI/SquadSpawn/SquadSpawnPoint.h"
#include "Variant_Shooter/AI/SquadSpawn/SquadLoadout.h"

#include "Engine/World.h"
#include "EngineUtils.h"

AFactionHq::AFactionHq()
{
	// A headquarters is a place first. The point half of it - the sphere, the garrison, the loot,
	// the presence count - is the parent's, and only the role is not negotiable.
	PoiRole = EPoiRole::Headquarters;
	InfluenceRadius = 6000.0f;
}

void AFactionHq::BeginPlay()
{
	// StartingTeam before Super, because Super is what hands the point to the director, and a
	// headquarters that spends its first second neutral is a headquarters somebody can walk into.
	StartingTeam = FactionTeamId;

	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	if (URunDirectorSubsystem* Director = GetDirector())
	{
		Director->RegisterHq(this);
	}

	SortieTimer = FirstSortieDelaySeconds;
}

void AFactionHq::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HasAuthority())
	{
		if (URunDirectorSubsystem* Director = GetDirector())
		{
			Director->UnregisterHq(this);
		}
	}

	Super::EndPlay(Reason);
}

void AFactionHq::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority() || (Sorties.IsEmpty() && WeakenedSorties.IsEmpty()))
	{
		return;
	}

	SortieTimer -= DeltaSeconds;
	if (SortieTimer > 0.0f)
	{
		return;
	}

	SortieTimer = SortieIntervalSeconds;
	TrySendSortie();
}

void AFactionHq::NotifyBannerBroken(ABannerActor* BrokenBanner, AActor* Breaker)
{
	Super::NotifyBannerBroken(BrokenBanner, Breaker);

	// The parent spills loot for anything that is not a headquarters. Here the consequence is the
	// list this place is allowed to draw from for the rest of the run.
	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] HQ %s banner down: %d sorties left in the weakened list"),
		*PoiTag.ToString(), WeakenedSorties.Num());
}

USquadLoadout* AFactionHq::PickSortieLoadout() const
{
	// One line decides which war this headquarters is still fighting.
	const TArray<FSortieEntry>& Table = IsBannerBroken() ? WeakenedSorties : Sorties;

	float TotalWeight = 0.0f;
	for (const FSortieEntry& Entry : Table)
	{
		if (Entry.Loadout && Entry.Weight > 0.0f)
		{
			TotalWeight += Entry.Weight;
		}
	}

	if (TotalWeight <= 0.0f)
	{
		return nullptr;
	}

	float Roll = FMath::FRand() * TotalWeight;
	for (const FSortieEntry& Entry : Table)
	{
		if (!Entry.Loadout || Entry.Weight <= 0.0f)
		{
			continue;
		}

		Roll -= Entry.Weight;
		if (Roll <= 0.0f)
		{
			return Entry.Loadout;
		}
	}

	return nullptr;
}

void AFactionHq::TrySendSortie()
{
	URunDirectorSubsystem* Director = GetDirector();
	UWorld* World = GetWorld();
	if (!Director || !World)
	{
		return;
	}

	if (MaxSorties > 0 && SortiesSent >= MaxSorties)
	{
		return;
	}

	FVector Objective = FVector::ZeroVector;
	FName TargetTag = NAME_None;
	if (!Director->GetSortieTarget(FactionTeamId, GetActorLocation(), Objective, TargetTag))
	{
		// Nothing on the map this faction does not already hold. Standing still is the honest
		// answer; marching somewhere for the look of it is how you get squads walking in circles.
		return;
	}

	USquadLoadout* Loadout = PickSortieLoadout();
	if (!Loadout)
	{
		return;
	}

	if (Loadout->InitialTask != ESquadInitialTask::Attack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MAP_DEBUG] HQ %s is sending %s, which is set to Defend. It will hold at the gate."),
			*PoiTag.ToString(), *Loadout->GetName());
	}

	if (Loadout->FactionTeamId != FactionTeamId)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MAP_DEBUG] HQ %s (team %d) is sending %s, which is team %d."),
			*PoiTag.ToString(), FactionTeamId, *Loadout->GetName(), Loadout->FactionTeamId);
	}

	// The gate, when one is named: squads that spawn inside their own base walk out through it.
	FVector Origin = GetActorLocation();
	if (!SortieSpawnPointTag.IsNone())
	{
		for (TActorIterator<ASquadSpawnPoint> It(World); It; ++It)
		{
			if (It->PointTag == SortieSpawnPointTag)
			{
				Origin = It->GetActorLocation();
				break;
			}
		}
	}

	USquadSpawnSubsystem* Squads = World->GetSubsystem<USquadSpawnSubsystem>();
	if (!Squads)
	{
		return;
	}

	const int32 Spawned = Squads->SpawnSquadMembers(Origin, SortieScatterRadius, Loadout, &Objective);
	if (Spawned > 0)
	{
		++SortiesSent;
		UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] HQ %s sortie %d: %d members of %s -> %s"),
			*PoiTag.ToString(), SortiesSent, Spawned, *Loadout->GetName(), *TargetTag.ToString());
	}
}
