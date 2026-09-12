// ScoutStateTreeTasks.cpp

#include "Variant_Shooter/AI/ScoutStateTreeTasks.h"

#include "AI/FactionContactMemory.h"
#include "AI/PolarityTeams.h"
#include "AI/TacticalSpace.h"
#include "Coop/CoopPlayers.h"
#include "Variant_Shooter/AI/FlyingDrone.h"
#include "Variant_Shooter/AI/FlyingAIMovementComponent.h"
#include "Variant_Shooter/AI/SquadSpawn/Squad.h"
#include "Variant_Shooter/AI/SquadSpawn/SquadSpawnSubsystem.h"
#include "Variant_Shooter/Map/MapEventTypes.h"
#include "Variant_Shooter/Map/RunDirectorSubsystem.h"

#include "AIController.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "StateTreeExecutionContext.h"

namespace
{
	/** Roughly where a pair of eyes sits, so line-of-sight traces do not start in the floor. */
	constexpr float EyeHeight = 160.0f;

	bool HasClearLine(const UWorld* World, const AActor* Ignore, const FVector& From, const FVector& To)
	{
		if (!World)
		{
			return false;
		}

		FCollisionQueryParams Params(SCENE_QUERY_STAT(ScoutSight), /*bTraceComplex*/ false, Ignore);
		return !World->LineTraceTestByChannel(From, To, ECC_Visibility, Params);
	}

	/** The operator's own squad, which is where these tasks keep everything. Null for a pawn nobody
	 *  spawned through the squad system, and every task treats that as "cannot work". */
	USquad* SquadOf(const AActor* Actor)
	{
		const APawn* const Pawn = Cast<APawn>(Actor);
		const UWorld* const World = Pawn ? Pawn->GetWorld() : nullptr;
		const USquadSpawnSubsystem* const Squads =
			World ? World->GetSubsystem<USquadSpawnSubsystem>() : nullptr;

		return Squads ? Squads->FindSquadOfPawn(Pawn) : nullptr;
	}
}

//////////////////////////////////////////////////////////////////
// Pick Observation Post
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreePickObservationPostTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	USquad* const Squad = SquadOf(Data.Actor);
	const UWorld* const World = Data.Actor ? Data.Actor->GetWorld() : nullptr;
	URunDirectorSubsystem* const Director = World ? World->GetSubsystem<URunDirectorSubsystem>() : nullptr;
	if (!Squad || !Director)
	{
		return EStateTreeRunStatus::Failed;
	}

	FVector Post = FVector::ZeroVector;
	FName WatchTag = NAME_None;

	// Failing here is a normal answer, not an error: on a map this side has just walked over there
	// is genuinely nothing it does not already know, and the tree should idle rather than march.
	if (!Director->FindScoutPost(PolarityTeams::GetTeam(Data.Actor), Data.Actor->GetActorLocation(),
		Post, WatchTag))
	{
		return EStateTreeRunStatus::Failed;
	}

	Squad->ObjectiveTag = WatchTag;
	Squad->Objective = Post;

	UE_LOG(LogTemp, Verbose, TEXT("[SCOUT_DEBUG] %s will watch %s from (%.0f, %.0f)"),
		*Data.Actor->GetName(), *WatchTag.ToString(), Post.X, Post.Y);

	return EStateTreeRunStatus::Succeeded;
}

//////////////////////////////////////////////////////////////////
// Drone Fly To Post
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeDroneFlyToPostTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	const USquad* const Squad = SquadOf(Data.Drone);
	if (!Data.Drone || Data.Drone->IsDead() || !Squad)
	{
		return EStateTreeRunStatus::Failed;
	}

	UFlyingAIMovementComponent* const Flight = Data.Drone->GetFlyingMovement();
	if (!Flight)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Keep the drone's own altitude. The post is a place on the map, not a place in the air, and
	// the director has no business deciding how high anything flies.
	FVector Destination = Squad->Objective;
	Destination.Z = Data.Drone->GetActorLocation().Z;

	FVector Projected = Destination;
	if (Flight->ProjectToNavMesh(Destination, Projected))
	{
		Projected.Z = Destination.Z;
	}

	Flight->FlyToLocation(Projected);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeDroneFlyToPostTask::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	return Data.Drone->IsFlying() ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
}

//////////////////////////////////////////////////////////////////
// Move To Post
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeMoveToPostTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	const USquad* const Squad = SquadOf(Data.Actor);
	if (!Data.Controller || !Squad)
	{
		return EStateTreeRunStatus::Failed;
	}

	Data.Controller->MoveToLocation(Squad->Objective, Data.AcceptanceRadius,
		/*bStopOnOverlap*/ true, /*bUsePathfinding*/ true,
		/*bProjectDestinationToNavigation*/ true, /*bCanStrafe*/ false);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeMoveToPostTask::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Controller)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Arrival is the engine's word, not ours. Its reach test adds the goal and agent radii to the
	// acceptance radius, so a pawn legitimately parked at ninety-odd units reads as "not there yet"
	// to any hand-rolled distance check, forever (Docs/Gotchas/AI_StateTree.md).
	switch (Data.Controller->GetMoveStatus())
	{
	case EPathFollowingStatus::Idle:
		return EStateTreeRunStatus::Succeeded;

	case EPathFollowingStatus::Moving:
	case EPathFollowingStatus::Waiting:
		return EStateTreeRunStatus::Running;

	default:
		return EStateTreeRunStatus::Failed;
	}
}

//////////////////////////////////////////////////////////////////
// Pick Sniper Nest
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreePickSniperNestTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	USquad* const Squad = SquadOf(Data.Actor);
	UWorld* const World = Data.Actor ? Data.Actor->GetWorld() : nullptr;
	UNavigationSystemV1* const Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	const URunDirectorSubsystem* const Director = World ? World->GetSubsystem<URunDirectorSubsystem>() : nullptr;
	if (!Squad || !Nav || !Director)
	{
		return EStateTreeRunStatus::Failed;
	}

	FPoiWarState Watched;
	if (!Director->GetPoiState(Squad->ObjectiveTag, Watched))
	{
		return EStateTreeRunStatus::Failed;
	}

	// The eyes we are hiding FROM. Every point on the map that is not the one being watched: a spot
	// overlooked by three neighbouring compounds is a spot somebody walks into eventually, however
	// good its view of the target is.
	TArray<FVector> Eyes;
	for (const FPoiWarState& State : Director->GetAllPoiStates())
	{
		if (State.PoiTag != Watched.PoiTag)
		{
			Eyes.Add(State.Location + FVector(0.0f, 0.0f, EyeHeight));
		}
	}

	// Own ground versus theirs, so a nest is not chosen in the middle of an enemy formation. A
	// sniper wants distance from everybody, so the ally term is switched off entirely.
	TacticalSpace::FSpaceContext Space;
	TacticalSpace::BuildContext(Data.Actor, Space);
	TacticalSpace::FSpaceWeights Weights;
	Weights.AllyWeight = 0.0f;
	Weights.EnemyWeight = 1.0f;

	const FVector Watch = Watched.Location + FVector(0.0f, 0.0f, EyeHeight);
	const FVector Approach = Data.Actor->GetActorLocation();
	const float MinRange = FMath::Min(Data.MinRange, Data.MaxRange);
	const float MaxRange = FMath::Max(Data.MinRange, Data.MaxRange);
	const int32 Tries = FMath::Max(Data.Candidates, 4);

	FVector Best = FVector::ZeroVector;
	float BestScore = 0.0f;
	bool bFound = false;

	for (int32 Index = 0; Index < Tries; ++Index)
	{
		const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
		const float Range = FMath::FRandRange(MinRange, MaxRange);
		const FVector Raw = Watched.Location
			+ FVector(FMath::Cos(Angle) * Range, FMath::Sin(Angle) * Range, 0.0f);

		FNavLocation OnMesh;
		if (!Nav->ProjectPointToNavigation(Raw, OnMesh, FVector(600.0f, 600.0f, 2000.0f)))
		{
			continue;
		}

		const FVector Eye = OnMesh.Location + FVector(0.0f, 0.0f, EyeHeight);

		// Non-negotiable: a nest you cannot shoot from is not a nest.
		if (!HasClearLine(World, Data.Actor, Eye, Watch))
		{
			continue;
		}

		// Concealment: how many other eyes on the map can see this spot. Fewer is better, and this
		// is the whole difference between "a place with a view" and "a place to hide with a view".
		int32 Exposed = 0;
		for (const FVector& Watcher : Eyes)
		{
			if (HasClearLine(World, Data.Actor, Watcher, Eye))
			{
				++Exposed;
			}
		}

		// Walking there costs something too, or he crosses the map to sit two metres better.
		const float Reach = FVector::Dist(Approach, OnMesh.Location);

		float Score = -2.0f * float(Exposed);
		Score += TacticalSpace::ScorePosition(Space, OnMesh.Location, Weights);
		Score -= Reach / 20000.0f;

		if (!bFound || Score > BestScore)
		{
			BestScore = Score;
			Best = OnMesh.Location;
			bFound = true;
		}
	}

	// Nowhere with a view he can also hide in. A normal answer on open ground, and the tree should
	// go and look somewhere else rather than plant him in the first ditch.
	if (!bFound)
	{
		return EStateTreeRunStatus::Failed;
	}

	// The rough post becomes an actual firing position. ObjectiveTag is left alone: what he watches
	// has not changed, only where he watches it from.
	Squad->Objective = Best;

	UE_LOG(LogTemp, Verbose, TEXT("[SCOUT_DEBUG] %s nest on %s: exposure score %.2f"),
		*Data.Actor->GetName(), *Watched.PoiTag.ToString(), BestScore);

	return EStateTreeRunStatus::Succeeded;
}

//////////////////////////////////////////////////////////////////
// Route Is Clear
//////////////////////////////////////////////////////////////////

bool FStateTreeRouteIsClearCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	const USquad* const Squad = SquadOf(Data.Actor);
	if (!Data.Actor || !Squad)
	{
		return false;
	}

	const UFactionContactMemory* const Memory = UFactionContactMemory::Get(Data.Actor);
	if (!Memory)
	{
		// Nothing known is not the same as nothing there, but it is the only honest answer: a side
		// with no memory has no reason to divert.
		return true;
	}

	TArray<FFactionContact> Contacts;
	Memory->GetContacts(PolarityTeams::GetTeam(Data.Actor), Contacts);

	const FVector Start = Data.Actor->GetActorLocation();
	const float CorridorSq = FMath::Square(FMath::Max(Data.Corridor, 100.0f));

	for (const FFactionContact& Contact : Contacts)
	{
		// Distance from the straight line rather than from either end: a man standing beside the
		// road is exactly the one worth walking around, and he is far from both the start and the
		// destination.
		const FVector Closest = FMath::ClosestPointOnSegment(Contact.LastKnownLocation, Start, Squad->Objective);
		if (FVector::DistSquared(Closest, Contact.LastKnownLocation) < CorridorSq)
		{
			return false;
		}
	}

	return true;
}

//////////////////////////////////////////////////////////////////
// Shot Would Matter
//////////////////////////////////////////////////////////////////

bool FStateTreeShotWouldMatterCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Actor || !Data.Target)
	{
		return false;
	}

	// ---- The player branch ----
	//
	// He shoots a player only while other enemies are already on him. Not out of mercy: a sniper who
	// picks at a lone player turns the run into "go and find the sniper", which is a different game
	// and a worse one. Firing into a fight the player is already in takes the shot he cannot answer
	// and leaves him with the fight he was already having.
	if (CoopPlayers::IsPlayer(Data.Target))
	{
		TArray<APawn*> AgainstPlayer;
		PolarityTeams::GatherHostilePawns(Data.Target, AgainstPlayer);

		const FVector At = Data.Target->GetActorLocation();
		const float NearSq = FMath::Square(FMath::Max(Data.PlayerCompanyRadius, 200.0f));

		for (const APawn* const Pawn : AgainstPlayer)
		{
			if (Pawn && Pawn != Data.Actor && FVector::DistSquared(Pawn->GetActorLocation(), At) < NearSq)
			{
				return true;
			}
		}

		return false;
	}

	// ---- The faction branch ----
	//
	// A bullet is worth spending where it decides something. A fight that is already lost swallows
	// it; a fight already won did not need it.
	const USquad* const Squad = SquadOf(Data.Actor);
	const UWorld* const World = Data.Actor->GetWorld();
	const URunDirectorSubsystem* const Director = World ? World->GetSubsystem<URunDirectorSubsystem>() : nullptr;
	if (!Squad || !Director || Squad->ObjectiveTag.IsNone())
	{
		return false;
	}

	FPoiWarState State;
	if (!Director->GetPoiState(Squad->ObjectiveTag, State) || !State.bContested)
	{
		return false;
	}

	const uint8 Side = PolarityTeams::GetTeam(Data.Actor);
	const int32 Ours = (Side == PolarityTeams::FactionA) ? State.PresentA : State.PresentB;
	const int32 Theirs = (Side == PolarityTeams::FactionA) ? State.PresentB : State.PresentA;

	return FMath::Abs(Theirs - Ours) <= Data.CloseFightMargin;
}

//////////////////////////////////////////////////////////////////
// Position Compromised
//////////////////////////////////////////////////////////////////

bool FStateTreePositionCompromisedCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Actor)
	{
		return false;
	}

	TArray<APawn*> Hostiles;
	PolarityTeams::GatherHostilePawns(Data.Actor, Hostiles);

	const UWorld* const World = Data.Actor->GetWorld();
	const FVector Here = Data.Actor->GetActorLocation() + FVector(0.0f, 0.0f, EyeHeight);
	const float RadiusSq = FMath::Square(FMath::Max(Data.Radius, 200.0f));

	for (const APawn* const Pawn : Hostiles)
	{
		if (!Pawn || FVector::DistSquared(Pawn->GetActorLocation(), Data.Actor->GetActorLocation()) > RadiusSq)
		{
			continue;
		}

		if (!Data.bRequireLineOfSight)
		{
			return true;
		}

		if (HasClearLine(World, Data.Actor, Pawn->GetActorLocation() + FVector(0.0f, 0.0f, EyeHeight), Here))
		{
			return true;
		}
	}

	return false;
}
