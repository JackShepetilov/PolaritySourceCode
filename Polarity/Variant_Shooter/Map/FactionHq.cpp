// FactionHq.cpp

#include "Variant_Shooter/Map/FactionHq.h"

#include "Variant_Shooter/Map/RunDirectorSubsystem.h"
#include "Variant_Shooter/Map/Banner.h"
#include "Variant_Shooter/AI/SquadSpawn/SquadSpawnSubsystem.h"
#include "Variant_Shooter/AI/SquadSpawn/SquadSpawnPoint.h"
#include "Variant_Shooter/AI/SquadSpawn/SquadLoadout.h"
#include "Variant_Shooter/AI/SquadSpawn/Squad.h"

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

	if (!HasAuthority())
	{
		return;
	}

	// Eyes first, and on their own clock: they are cheap, they are few, and a side that has run out
	// of them is planning on memories.
	TickScouts(DeltaSeconds);

	if (Sorties.IsEmpty() && WeakenedSorties.IsEmpty() && ReinforcementSorties.IsEmpty())
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

void AFactionHq::TickScouts(float DeltaSeconds)
{
	if (ScoutsWanted <= 0 || ScoutSorties.IsEmpty())
	{
		return;
	}

	// Forget the dead. Weak pointers make this the whole of the bookkeeping.
	Scouts.RemoveAll([](const TWeakObjectPtr<APawn>& Weak)
	{
		const APawn* const Pawn = Weak.Get();
		return !Pawn || Pawn->IsPendingKillPending();
	});

	if (Scouts.Num() >= ScoutsWanted)
	{
		// Nobody to replace. The clock is held at zero rather than left running, so the first death
		// costs the full delay instead of whatever happened to be left over.
		ScoutTimer = ScoutRespawnSeconds;
		return;
	}

	ScoutTimer -= DeltaSeconds;
	if (ScoutTimer > 0.0f)
	{
		return;
	}
	ScoutTimer = ScoutRespawnSeconds;

	UWorld* const World = GetWorld();
	URunDirectorSubsystem* const Director = World ? World->GetSubsystem<URunDirectorSubsystem>() : nullptr;
	USquadSpawnSubsystem* const Squads = World ? World->GetSubsystem<USquadSpawnSubsystem>() : nullptr;
	if (!Director || !Squads)
	{
		return;
	}

	FVector Post = FVector::ZeroVector;
	FName WatchTag = NAME_None;
	if (!Director->FindScoutPost(FactionTeamId, GetActorLocation(), Post, WatchTag))
	{
		// Nothing worth watching. Not an error: on a map this side has just walked over, there is
		// genuinely nothing it does not already know.
		return;
	}

	USquadLoadout* const Loadout = PickWeighted(ScoutSorties);
	if (!Loadout)
	{
		return;
	}

	// One man, marked as his own thing. The tag is the point he is watching, not a point he is
	// being sent to hold, and that difference is what the exemptions above are protecting.
	USquad* Squad = nullptr;
	const int32 Spawned = Squads->SpawnSquadMembers(GetActorLocation(), SortieScatterRadius, Loadout,
		&Post, /*MaxMembers*/ 1, WatchTag, &Squad);
	if (Spawned <= 0 || !Squad)
	{
		return;
	}

	Squad->bLoneOperator = true;

	TArray<APawn*> Fresh;
	FVector Unused = FVector::ZeroVector;
	Squad->GatherAlive(Fresh, Unused);
	for (APawn* const Pawn : Fresh)
	{
		Scouts.Add(Pawn);
	}

	UE_LOG(LogTemp, Log, TEXT("[WAR_DEBUG] HQ %s sent an observer to watch %s (%d/%d out)"),
		*PoiTag.ToString(), *WatchTag.ToString(), Scouts.Num(), ScoutsWanted);
}

void AFactionHq::NotifyBannerBroken(ABannerActor* BrokenBanner, AActor* Breaker)
{
	Super::NotifyBannerBroken(BrokenBanner, Breaker);

	// The parent spills loot for anything that is not a headquarters. Here the consequence is the
	// list this place is allowed to draw from for the rest of the run.
	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] HQ %s banner down: %d sorties left in the weakened list"),
		*PoiTag.ToString(), WeakenedSorties.Num());
}

USquadLoadout* AFactionHq::PickWeighted(const TArray<FSortieEntry>& Table)
{
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

int32 AFactionHq::LoadoutSize(const USquadLoadout* Loadout)
{
	if (!Loadout)
	{
		return 0;
	}

	int32 Total = 0;
	for (const FSquadLoadoutEntry& Entry : Loadout->Members)
	{
		if (Entry.NPCClass)
		{
			Total += Entry.Count;
		}
	}
	return Total;
}

USquadLoadout* AFactionHq::PickSortieLoadout(EFactionAction Action) const
{
	// Reinforcing wants a garrison, not an assault, so it has its own table when there is one.
	if (Action == EFactionAction::Reinforce && !ReinforcementSorties.IsEmpty())
	{
		if (USquadLoadout* Loadout = PickWeighted(ReinforcementSorties))
		{
			return Loadout;
		}
	}

	// One line decides which war this headquarters is still fighting.
	return PickWeighted(IsBannerBroken() ? WeakenedSorties : Sorties);
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

	// How many could actually walk out, worked out BEFORE asking where to go. The odds of an attack
	// depend on the size of the fist being made, so a headquarters that asks "is this worth it"
	// without saying what it is prepared to send gets told no every time.
	USquadSpawnSubsystem* const Population = World->GetSubsystem<USquadSpawnSubsystem>();
	int32 Budget = 0;
	if (Population && FactionPopulationCap > 0)
	{
		const int32 Standing = Population->CountAliveOnTeam(FactionTeamId);
		Budget = FactionPopulationCap - Standing;
		if (Budget < MinSortieMembers)
		{
			// Log, not Verbose. On the first run of this system faction A went quiet after its
			// fourth sortie and there was no way to tell a working ceiling from a broken timer:
			// the only proof was counting pawns across a dozen SQUAD_DEBUG lines by hand. A brake
			// that engages silently is indistinguishable from a bug.
			UE_LOG(LogTemp, Log,
				TEXT("[WAR_DEBUG] HQ %s holds its gates: %d/%d of the faction already standing"),
				*PoiTag.ToString(), Standing, FactionPopulationCap);
			return;
		}
	}

	// What a full sortie from this place looks like, capped by the ceiling. Sized off the assault
	// list because that is what most orders turn out to be; a reinforcement is usually smaller and
	// erring high here only makes the faction slightly braver than it should be.
	int32 ForceAvailable = LoadoutSize(PickWeighted(IsBannerBroken() ? WeakenedSorties : Sorties));
	if (Budget > 0)
	{
		ForceAvailable = FMath::Min(ForceAvailable, Budget);
	}

	FFactionOrder Order;
	if (!Director->GetFactionOrder(FactionTeamId, GetActorLocation(), ForceAvailable, Order))
	{
		// Nothing on the map this faction does not already hold, and nothing of its own is being
		// taken. Standing still is the honest answer; marching somewhere for the look of it is how
		// you get squads walking in circles.
		return;
	}

	FVector Objective = Order.Location;
	const FName TargetTag = Order.PoiTag;

	USquadLoadout* Loadout = PickSortieLoadout(Order.Action);
	if (!Loadout)
	{
		return;
	}

	// Under the ceiling it goes whole, near it it goes short. Budget is passed straight through as
	// the cap; a squad smaller than the loadout is the point of the exercise.
	if (Budget >= LoadoutSize(Loadout))
	{
		Budget = 0;   // 0 means "everything", so a full squad is not silently trimmed
	}

	if (Loadout->InitialTask != ESquadInitialTask::Attack)
	{
		// Not cosmetic. USquadSpawnSubsystem only advances members whose task is Attack
		// (TickSquadTasks skips everything else), so a Defend loadout given an objective spawns at
		// the gate and stays there for the rest of the run. That catches reinforcement lists most
		// easily, because a garrison asset is the obvious thing to reach for and a garrison asset
		// is Defend: what a relief force needs is the same composition with the marching task.
		UE_LOG(LogTemp, Warning,
			TEXT("[WAR_DEBUG] HQ %s is sending %s to %s, but that loadout is Defend: it will NOT march and will hold at the gate."),
			*PoiTag.ToString(), *Loadout->GetName(), *TargetTag.ToString());
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

	const int32 Spawned = Squads->SpawnSquadMembers(Origin, SortieScatterRadius, Loadout, &Objective,
		Budget, TargetTag);
	if (Spawned > 0)
	{
		++SortiesSent;
		// Tell the director these are on the road. Without it every sortie recomputes the odds as
		// though the previous one had never left, which is how an army feeds one squad at a time
		// into the same fight until there is nothing left to feed.
		Director->NotifySortieSent(FactionTeamId, TargetTag, Spawned);
		const int32 Full = LoadoutSize(Loadout);
		UE_LOG(LogTemp, Log,
			TEXT("[WAR_DEBUG] HQ %s sortie %d: %s %s with %d/%d of %s (score %.3f, %s)"),
			*PoiTag.ToString(), SortiesSent,
			Order.Action == EFactionAction::Attack ? TEXT("ATTACK") : TEXT("REINFORCE"),
			*TargetTag.ToString(), Spawned, Full, *Loadout->GetName(), Order.Score, *Order.Reason);
	}
}
