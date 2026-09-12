// Copyright Epic Games, Inc. All Rights Reserved.

#include "SquadSpawnSubsystem.h"
#include "Engine/OverlapResult.h"
#include "SquadSpawnPoint.h"
#include "SquadScenario.h"
#include "AIController.h"
#include "AI/FactionContactMemory.h"
#include "AI/BattleLog.h"
#include "Variant_Shooter/Map/RunDirectorSubsystem.h"
#include "AI/Components/AIAccuracyComponent.h"
#include "ShooterNPC.h"
#include "FlyingDrone.h"
#include "FlyingAIMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "AI/Navigation/PolarityPathFollowingComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "GenericTeamAgentInterface.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"

/** Set `squad.AutoRunScenario <path>` before PIE / on a headless command line and the scenario
 *  runs by itself once the world has settled. Empty (default) = no auto-run. */
static TAutoConsoleVariable<FString> CVarSquadAutoRunScenario(
	TEXT("squad.AutoRunScenario"),
	TEXT(""),
	TEXT("Content path of a USquadScenario asset to spawn automatically after world start. Empty = off."));

namespace
{
	constexpr float AutoRunDelaySeconds = 2.0f;
	/** Floor on how often a member's order may be refreshed. Two seconds was the first value and it
	 *  is what the escort's hopping actually was: fly to a slot, arrive, stand still until the timer
	 *  came round again by which time the slot was ten metres further on. */
	constexpr float AttackMoveReissueInterval = 0.5f;

	/** A slot that has travelled this far since the order was given is worth re-issuing */
	constexpr float FormationReissueDistance = 400.0f;
	constexpr float AttackEngageDistance = 1500.0f;
	constexpr float AttackArriveAcceptance = 300.0f;

	/** Last-resort leash. With the speed throttle below nobody should reach it; it exists for the
	 *  cases the throttle cannot cover, like a member that got teleported or had to detour. */
	constexpr float SquadCohesionRadius = 2500.0f;

	/** Never throttle a squad below this, or a badly hurt member turns the advance into a standstill */
	constexpr float MinMarchSpeed = 120.0f;

	/** Slot reached this closely counts as "in formation" */
	constexpr float FormationSlotAcceptance = 250.0f;

	/** Beyond this from its goal, a squad order overrides whatever the pawn was doing on its own.
	 *
	 *  Without it a member in contact never marches at all: its behaviour tree is always moving it
	 *  somewhere (cover, a peek, a strafe), the squad sees "already going somewhere" and politely
	 *  stands down - forever. Eighty metres from where you were sent is not a firefight you are
	 *  winning, it is the wrong end of the map. Inside this distance the trees own the pawn again,
	 *  which is where they should own it. */
	constexpr float MarchOverrideDistance = 3000.0f;

	/** How far survivors will look for a friendly squad to join before deciding to hold where they
	 *  are. Roughly "the next position over", not "anywhere on the map". */
	constexpr float MergeRadius = 4000.0f;

	/** How long one half of a withdrawing squad runs before the halves swap roles. Long enough to
	 *  cover ground, short enough that nobody is left standing in the open on their own. */
	constexpr float BoundSeconds = 3.0f;

	// ---- structure: splitting and merging ----

	/** Below this a detachment is not a squad. Three is the smallest number that can leave one man
	 *  shooting while two move, which is the least a squad has to be able to do. */
	constexpr int32 MinSquadSize = 3;

	/** How long a surplus has to PERSIST before anybody acts on it.
	 *
	 *  Demand moves every time somebody dies, so without this a squad detaches, the numbers swing
	 *  back, it merges, and it does that for the rest of the run. Soldiers jogging between two
	 *  points in a loop read as broken software, not as tactics. */
	constexpr float SurplusHoldSeconds = 15.0f;

	/** And a squad that was just split or merged is left alone this long, for the same reason from
	 *  the other side. */
	constexpr float RestructureCooldownSeconds = 30.0f;

	/** Two friendly squads this close, on the same objective, are one force. */
	constexpr float ObjectiveMergeRadius = 3500.0f;

	/** How often structure decisions are revisited. Tens of seconds, not frames. */
	constexpr float StructureTickSeconds = 3.0f;
}

/** Every squad drawn in the world, through walls: who they are, how many, and where they were sent.
 *
 *  A wallhack, on purpose and off by default. The log can say a squad is standing still; it cannot
 *  say whether the nine men in front of you are the ones the log is talking about. Cheat cvar, for
 *  working on the game rather than playing it. */
/** How much faster a squad moves while it is only marching.
 *
 *  Crossing a 1372 m map at walking pace is most of what an observer of this game watches, and it
 *  is the least interesting part of it. Soldiers who run between points also read correctly: you
 *  march to a fight, you do not stroll to one.
 *
 *  Only the approach is sprinted. Once a squad is inside engage range, or breaking away, everyone
 *  goes back to their own legs - the throttle exists to hold formation, and sprinting into a fight
 *  would hand the fast members a head start exactly where that gets them killed alone. */
static TAutoConsoleVariable<float> CVarMarchSprint(
	TEXT("polarity.squads.marchSprint"),
	1.6f,
	TEXT("Speed multiplier applied to squads while marching to a point. 1 disables it."),
	ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarSquadDraw(
	TEXT("polarity.squads.draw"),
	0,
	TEXT("Draw every squad in the world: members, task, objective. Sees through geometry."),
	ECVF_Cheat);

namespace
{
	/** Top speed of whatever moves this pawn. Two movement components in this project and no shared
	 *  interface between them: a ground NPC walks on the character movement component, a drone flies
	 *  on its own. */
	float GetMarchSpeed(const APawn* Pawn)
	{
		if (const AFlyingDrone* const Drone = Cast<AFlyingDrone>(Pawn))
		{
			if (const UFlyingAIMovementComponent* const Flying = Drone->GetFlyingMovement())
			{
				return Flying->FlySpeed;
			}
		}

		if (const ACharacter* const Character = Cast<ACharacter>(Pawn))
		{
			if (const UCharacterMovementComponent* const Movement = Character->GetCharacterMovement())
			{
				return Movement->MaxWalkSpeed;
			}
		}

		return 0.0f;
	}

	void SetMarchSpeed(APawn* Pawn, float Speed)
	{
		if (AFlyingDrone* const Drone = Cast<AFlyingDrone>(Pawn))
		{
			if (UFlyingAIMovementComponent* const Flying = Drone->GetFlyingMovement())
			{
				Flying->FlySpeed = Speed;
				return;
			}
		}

		if (ACharacter* const Character = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* const Movement = Character->GetCharacterMovement())
			{
				Movement->MaxWalkSpeed = Speed;
			}
		}
	}

	/** A drone does not walk. Its orders go to the flying movement component, its "am I still going"
	 *  is that component's business, and the AI controller's MoveTo is for pawns with feet.
	 *
	 *  Learned the hard way: MoveToLocation with a goal a few metres above the floor cannot project
	 *  onto the navmesh, so the request produced no path and the whole flight simply stopped. The
	 *  version before that "worked" only because it handed the drones a floor-level Z, which is why
	 *  they were skimming the boards. */
	bool IsMemberMoving(const APawn* Pawn, const AAIController* AI)
	{
		if (const AFlyingDrone* const Drone = Cast<AFlyingDrone>(Pawn))
		{
			return Drone->IsFlying();
		}

		const UPathFollowingComponent* const PathComp = AI ? AI->GetPathFollowingComponent() : nullptr;
		return PathComp && PathComp->GetStatus() == EPathFollowingStatus::Moving;
	}

	/** Give the order and say whether it was accepted.
	 *
	 *  The result was thrown away before, which is how a squad could report "marching" every two
	 *  seconds while standing perfectly still: MoveTo can refuse (no path, goal off the navmesh,
	 *  already there) and refusing looks exactly like obeying if nobody reads the answer. */
	const TCHAR* IssueMove(APawn* Pawn, AAIController* AI, const FVector& Destination, float Acceptance)
	{
		if (AFlyingDrone* const Drone = Cast<AFlyingDrone>(Pawn))
		{
			if (UFlyingAIMovementComponent* const Flying = Drone->GetFlyingMovement())
			{
				Flying->FlyToLocation(Destination, Acceptance);
				return TEXT("flying");
			}
		}

		if (!AI)
		{
			return TEXT("no controller");
		}

		switch (AI->MoveToLocation(Destination, Acceptance))
		{
		case EPathFollowingRequestResult::RequestSuccessful: return TEXT("marching");
		case EPathFollowingRequestResult::AlreadyAtGoal:     return TEXT("MoveTo says already at goal");
		case EPathFollowingRequestResult::Failed:            return TEXT("MoveTo FAILED (no path?)");
		default:                                             return TEXT("MoveTo unknown result");
		}
	}

	void StopMember(APawn* Pawn, AAIController* AI)
	{
		if (AFlyingDrone* const Drone = Cast<AFlyingDrone>(Pawn))
		{
			Drone->StopMovement();
			return;
		}

		if (AI)
		{
			AI->StopMovement();
		}
	}

	/** Where slot Index stands relative to the point of the formation, in march space:
	 *  +X is the direction of advance, +Y is its right. */
	FVector FormationSlotOffset(ESquadFormation Formation, int32 Index, float Spacing)
	{
		if (Index <= 0)
		{
			return FVector::ZeroVector;
		}

		switch (Formation)
		{
		case ESquadFormation::Column:
			return FVector(-Spacing * Index, 0.0f, 0.0f);

		case ESquadFormation::Line:
		{
			const int32 Row = (Index + 1) / 2;
			const float Side = (Index % 2 == 1) ? -1.0f : 1.0f;
			return FVector(0.0f, Side * Spacing * Row, 0.0f);
		}

		case ESquadFormation::Wedge:
		default:
		{
			// Alternate the sides so the squad grows evenly instead of trailing off to one flank,
			// and drop each pair further back to keep the point clear to fire through.
			const int32 Row = (Index + 1) / 2;
			const float Side = (Index % 2 == 1) ? -1.0f : 1.0f;
			return FVector(-Spacing * 0.8f * Row, Side * Spacing * Row, 0.0f);
		}
		}
	}

	USquadSpawnSubsystem* GetSubsystem(const UWorld* World)
	{
		return World ? World->GetSubsystem<USquadSpawnSubsystem>() : nullptr;
	}
}

// ==================== USubsystem ====================

void USquadSpawnSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!GetWorld())
	{
		return;
	}

	// Auto-run: once, shortly after the world is ready, if the cvar names a scenario
	if (!bAutoRunFired)
	{
		ElapsedSinceStart += DeltaTime;
		if (ElapsedSinceStart >= AutoRunDelaySeconds)
		{
			bAutoRunFired = true;

			const FString& Path = CVarSquadAutoRunScenario.GetValueOnGameThread();
			if (!Path.IsEmpty())
			{
				if (USquadScenario* Scenario = Cast<USquadScenario>(FSoftObjectPath(Path).TryLoad()))
				{
					const int32 Spawned = RunScenario(Scenario);
					UE_LOG(LogTemp, Warning, TEXT("[SQUAD_DEBUG] auto-run '%s' -> %d members"),
						*Path, Spawned);
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("[SQUAD_DEBUG] auto-run failed to load scenario '%s'"), *Path);
				}
			}
		}
	}

	TickSquadNerve();
	TickSquadTasks(DeltaTime);
	CheckFightOver();

	// Structure runs on its own slow clock. Splitting and merging are decisions measured in tens of
	// seconds, and asking every frame would only give the answer more chances to flicker.
	StructureTimer += DeltaTime;
	if (StructureTimer >= StructureTickSeconds)
	{
		StructureTimer = 0.0f;
		TickSquadStructure();
	}

	if (CVarSquadDraw.GetValueOnGameThread() != 0)
	{
		DrawSquadDebug();
	}

	// A squad-level heartbeat, because "nobody advanced" is impossible to diagnose from the outside:
	// it looks the same whether the order never came, the goal was somewhere unexpected, or they
	// decided they were already in contact. Filter on [SQUAD_DEBUG].
	SquadReportTimer += DeltaTime;
	if (SquadReportTimer >= 2.0f)
	{
		SquadReportTimer = 0.0f;
		ReportSquads();
	}
}

// ==================== Registry ====================

void USquadSpawnSubsystem::RegisterSpawnPoint(ASquadSpawnPoint* Point)
{
	if (Point && !SpawnPoints.Contains(Point))
	{
		SpawnPoints.Add(Point);
	}
}

void USquadSpawnSubsystem::UnregisterSpawnPoint(ASquadSpawnPoint* Point)
{
	SpawnPoints.RemoveAll([Point](const TWeakObjectPtr<ASquadSpawnPoint>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Point;
	});
}

TArray<ASquadSpawnPoint*> USquadSpawnSubsystem::GetPointsByTag(FName Tag) const
{
	TArray<ASquadSpawnPoint*> Found;
	for (const TWeakObjectPtr<ASquadSpawnPoint>& Entry : SpawnPoints)
	{
		if (Entry.IsValid() && Entry->PointTag == Tag)
		{
			Found.Add(Entry.Get());
		}
	}
	return Found;
}

ASquadSpawnPoint* USquadSpawnSubsystem::FindFirstPointByTag(FName Tag) const
{
	const TArray<ASquadSpawnPoint*> Points = GetPointsByTag(Tag);
	return Points.Num() > 0 ? Points[0] : nullptr;
}

// ==================== Spawning ====================

int32 USquadSpawnSubsystem::SpawnAtTag(FName Tag, USquadLoadout* Loadout)
{
	const TArray<ASquadSpawnPoint*> Points = GetPointsByTag(Tag);
	if (Points.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[SQUAD_DEBUG] no spawn point tagged '%s'"), *Tag.ToString());
		return -1;
	}

	int32 Total = 0;
	for (ASquadSpawnPoint* Point : Points)
	{
		Total += Point->SpawnSquad(Loadout);
	}

	return Total;
}

int32 USquadSpawnSubsystem::RunScenario(USquadScenario* Scenario)
{
	if (!Scenario)
	{
		return -1;
	}

	// Wipe the last run first. Without this a second scenario in the same session inherits the
	// previous one's squads and their morale records, and a squad that started as three men reports
	// itself as three of eleven: the counts are the sum of every run since the editor opened.
	ClearAll();
	Squads.Reset();

	int32 Total = 0;
	bool bAnyPointMatched = false;

	for (const FSquadScenarioEntry& Entry : Scenario->Entries)
	{
		if (!Entry.Loadout || Entry.PointTag.IsNone())
		{
			continue;
		}

		const TArray<ASquadSpawnPoint*> Points = GetPointsByTag(Entry.PointTag);
		if (Points.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SQUAD_DEBUG] scenario: no point tagged '%s'"), *Entry.PointTag.ToString());
			continue;
		}

		bAnyPointMatched = true;

		// Objective resolved here, once, from the tag: spawn points know where they are, and squads
		// then carry a position rather than a name for the rest of the fight.
		FVector Objective = FVector::ZeroVector;
		bool bHasObjective = false;
		if (!Entry.TargetPointTag.IsNone())
		{
			if (const ASquadSpawnPoint* const TargetPoint = FindFirstPointByTag(Entry.TargetPointTag))
			{
				Objective = TargetPoint->GetActorLocation();
				bHasObjective = true;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[SQUAD_DEBUG] scenario: target tag '%s' matches no point"),
					*Entry.TargetPointTag.ToString());
			}
		}

		for (ASquadSpawnPoint* Point : Points)
		{
			Total += Point->SpawnSquad(Entry.Loadout, bHasObjective ? &Objective : nullptr);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[SQUAD_DEBUG] scenario '%s': %d members spawned"),
		*Scenario->GetName(), Total);

	// A scenario run IS a fight: one record per run, cleared here so two runs never blend.
	if (UBattleLogSubsystem* const Battle = UBattleLogSubsystem::Get(this))
	{
		Battle->BeginFight(Scenario->GetName());
	}
	bFightFinished = false;

	return bAnyPointMatched ? Total : -1;
}

int32 USquadSpawnSubsystem::ClearAll()
{
	int32 Removed = 0;
	for (const FTrackedSquadMember& Member : Members)
	{
		if (Member.Pawn.IsValid())
		{
			Member.Pawn->Destroy();
			++Removed;
		}
	}

	Members.Reset();
	Squads.Reset();

	UE_LOG(LogTemp, Warning, TEXT("[SQUAD_DEBUG] cleared %d members"), Removed);
	return Removed;
}

int32 USquadSpawnSubsystem::CountAliveOnTeam(uint8 TeamId) const
{
	int32 Alive = 0;
	for (const FTrackedSquadMember& Member : Members)
	{
		const APawn* Pawn = Member.Pawn.Get();
		if (Member.TeamId == TeamId && Pawn && IsValid(Pawn))
		{
			++Alive;
		}
	}
	return Alive;
}

int32 USquadSpawnSubsystem::SpawnSquadMembers(const FVector& Origin, float ScatterRadius, USquadLoadout* Loadout,
	const FVector* Objective, int32 MaxMembers, FName ObjectiveTag, USquad** OutSquad)
{
	if (OutSquad)
	{
		*OutSquad = nullptr;
	}

	UWorld* World = GetWorld();
	if (!Loadout || !World || Loadout->Members.Num() == 0)
	{
		return 0;
	}

	// Expand entries into an ordered class list, carrying the "this row provides the commander"
	// mark alongside it so the choice survives the expansion.
	TArray<TSubclassOf<APawn>> ClassList;
	TArray<bool> CommanderMarks;
	for (const FSquadLoadoutEntry& Entry : Loadout->Members)
	{
		if (!Entry.NPCClass)
		{
			continue;
		}
		for (int32 i = 0; i < Entry.Count; ++i)
		{
			ClassList.Add(Entry.NPCClass);
			CommanderMarks.Add(Entry.bProvidesCommander);
		}
	}

	if (ClassList.Num() == 0)
	{
		return 0;
	}

	// The cut, when the caller is on a budget. Done here rather than by handing over a smaller
	// loadout, because a loadout is an asset a designer owns and half of one is not a thing that
	// should exist on disk.
	if (MaxMembers > 0 && ClassList.Num() > MaxMembers)
	{
		// Pull a marked commander into the surviving half first. Cutting the tail off a list whose
		// only marked row sits at the end leaves a squad that cannot break in an orderly way, and
		// that reads as a bug rather than as a smaller squad.
		int32 MarkedIndex = INDEX_NONE;
		for (int32 i = 0; i < CommanderMarks.Num(); ++i)
		{
			if (CommanderMarks[i])
			{
				MarkedIndex = i;
				break;
			}
		}
		if (MarkedIndex >= MaxMembers)
		{
			ClassList.Swap(0, MarkedIndex);
			CommanderMarks.Swap(0, MarkedIndex);
		}

		ClassList.SetNum(MaxMembers);
		CommanderMarks.SetNum(MaxMembers);
		UE_LOG(LogTemp, Verbose, TEXT("[WAR_DEBUG] squad cut to %d members by the caller's budget"),
			MaxMembers);
	}

	// One call, one squad. Two loadouts sent to the same spawn point are two squads that happen to
	// share a doorstep, and everything downstream - morale, commander, the merge - depends on
	// telling them apart.
	const int32 SquadId = NextSquadId++;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	const float GoldenAngle = PI * (3.0f - FMath::Sqrt(5.0f));

	int32 Spawned = 0;
	const int32 Total = ClassList.Num();

	for (int32 Index = 0; Index < Total; ++Index)
	{
		// Golden-angle ring: even spread without grid regularity
		const float Angle = Index * GoldenAngle + FMath::FRandRange(-0.15f, 0.15f);
		const float Radius = ScatterRadius * FMath::Sqrt((Index + 0.5f) / Total) + FMath::FRandRange(0.0f, 50.0f);

		FVector Location = Origin + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 50.0f);
		const FRotator Rotation(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f);

		APawn* Pawn = World->SpawnActor<APawn>(ClassList[Index], Location, Rotation, Params);
		if (!Pawn)
		{
			UE_LOG(LogTemp, Error, TEXT("[SQUAD_DEBUG] failed to spawn member %s"), *ClassList[Index]->GetName());
			continue;
		}

		Pawn->SpawnDefaultController();

		// The pawn AND its controller both carry the team id, and they must not disagree
		// (PolarityCharacter comment: the direction of the query decides who is asked)
		const FGenericTeamId TeamId(Loadout->FactionTeamId);
		if (IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(Pawn))
		{
			TeamAgent->SetGenericTeamId(TeamId);
		}
		if (IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(Pawn->GetController()))
		{
			TeamAgent->SetGenericTeamId(TeamId);
		}

		FTrackedSquadMember Member;
		Member.Pawn = Pawn;
		Member.TeamId = Loadout->FactionTeamId;
		Member.Task = Loadout->InitialTask;
		Member.SquadId = SquadId;
		Member.SquadOrigin = Origin;
		Member.NextMoveCheckTime = 0.0f;
		Member.Formation = Loadout->Formation;
		Member.FormationSpacing = Loadout->FormationSpacing;
		Member.SlotIndex = Spawned;
		Member.BaseSpeed = GetMarchSpeed(Pawn);
		if (Objective)
		{
			Member.Objective = *Objective;
			Member.bHasObjective = true;
		}
		Members.Add(Member);

		// The squad is created by whoever gets here first and then grows. USquad owns the roster AND
		// the nerve, so the count and the list of bodies can no longer drift apart the way two arrays
		// keyed by the same id could.
		USquad* Squad = FindSquad(SquadId);
		if (!Squad)
		{
			Squad = NewObject<USquad>(this);
			Squad->SquadId = SquadId;
			Squad->TeamId = Loadout->FactionTeamId;
			Squad->Anchor = Origin;
			Squad->WithdrawStrength = Loadout->WithdrawStrength;
			Squad->RegroupSeconds = Loadout->RegroupSeconds;
			Squad->Task = Loadout->InitialTask == ESquadInitialTask::Attack
				? ESquadTask::Advance : ESquadTask::Hold;
			Squad->ObjectiveTag = ObjectiveTag;
			if (Objective)
			{
				Squad->Objective = *Objective;
				Squad->bHasObjective = true;
			}
			else
			{
				// A garrison is FOR the place it stands on even though nobody sent it anywhere, and
				// the structure tick needs a position to compare against, not just a name.
				Squad->Objective = Origin;
				Squad->bHasObjective = !ObjectiveTag.IsNone();
			}
			Squads.Add(Squad);
		}
		Squad->AddMember(Pawn);
		Squad->InitialCount++;
		Squad->LastAliveCount = Squad->InitialCount;

		// Whoever the loadout put in charge. Nothing marked falls back to the first one out of the
		// truck, so a squad always has somebody whose death changes how it breaks.
		const bool bMarkedCommander = CommanderMarks.IsValidIndex(Index) && CommanderMarks[Index];
		const bool bTakesCommand = bMarkedCommander
			? !Squad->bCommanderWasChosen
			: !Squad->Commander.IsValid();

		if (bTakesCommand)
		{
			Squad->Commander = Pawn;
			Squad->bCommanderWasChosen = bMarkedCommander;

			UE_LOG(LogTemp, Warning, TEXT("[SQUAD_DEBUG] %s takes command of squad team=%d at (%.0f, %.0f)%s"),
				*Pawn->GetName(), Loadout->FactionTeamId, Origin.X, Origin.Y,
				bMarkedCommander ? TEXT("") : TEXT(" (nobody marked, first in)"));
		}

		if (UBattleLogSubsystem* const Battle = UBattleLogSubsystem::Get(this))
		{
			Battle->RecordSpawn(Pawn, Loadout->FactionTeamId);
		}

		++Spawned;

		UE_LOG(LogTemp, Warning, TEXT("[SQUAD_DEBUG] spawned %s team=%d task=%d at (%.0f, %.0f)"),
			*Pawn->GetName(), Loadout->FactionTeamId, static_cast<int32>(Loadout->InitialTask), Location.X, Location.Y);
	}

	// Handed back so the caller can mark what it just made. bLoneOperator is the case this exists
	// for: the spawn path has no way to know a loadout of one is a scout rather than a small squad.
	if (OutSquad)
	{
		*OutSquad = FindSquad(SquadId);
	}

	return Spawned;
}

// ==================== Squad Tasks ====================

USquad* USquadSpawnSubsystem::FindSquad(int32 SquadId) const
{
	for (USquad* const Squad : Squads)
	{
		if (Squad && Squad->SquadId == SquadId)
		{
			return Squad;
		}
	}
	return nullptr;
}

void USquadSpawnSubsystem::TickSquadNerve()
{
	const UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	for (USquad* const SquadPtr : Squads)
	{
		if (!SquadPtr)
		{
			continue;
		}
		USquad& Nerve = *SquadPtr;

		// A one-man squad loses a hundred percent of its strength the moment that man is hurt, so
		// nerve on a lone operator says "broken" from the first scratch and he spends the run
		// withdrawing. Whether he runs is his own StateTree's decision, and it is a different
		// decision: a scout leaves because he was seen, not because the squad is losing.
		if (Nerve.bLoneOperator || Nerve.InitialCount <= 0)
		{
			continue;
		}

		int32 Alive = 0;
		for (const FTrackedSquadMember& Member : Members)
		{
			if (Member.SquadId != Nerve.SquadId)
			{
				continue;
			}

			const APawn* const Pawn = Member.Pawn.Get();
			if (Pawn && !Pawn->IsPendingKillPending())
			{
				++Alive;
			}
		}

		if (Alive < Nerve.LastAliveCount)
		{
			Nerve.LastLossTime = Now;

			// Наверх, в штаб. Здесь единственное место во всей системе, где убыль вообще заметна:
			// отряд знает и сколько его осталось, и за какую точку он послан. Без этого доклада
			// планировщик считает по текущим силам врага и не помнит, что тут уже положили два
			// отряда - отсюда и ровный ручей подкреплений в мясорубку.
			if (URunDirectorSubsystem* const Dir = GetWorld()
				? GetWorld()->GetSubsystem<URunDirectorSubsystem>() : nullptr)
			{
				Dir->NotifyLosses(Nerve.TeamId, Nerve.ObjectiveTag, Nerve.LastAliveCount - Alive);
			}
		}

		// Notice the commander going down the moment it happens: it changes what a break looks like,
		// so it is worth a line in the battle log even when the squad is still standing.
		if (Nerve.Commander.IsStale() || (Nerve.Commander.IsValid() && Nerve.Commander->IsPendingKillPending()))
		{
			if (UBattleLogSubsystem* const Battle = UBattleLogSubsystem::Get(this))
			{
				Battle->RecordNote(TEXT("commander down"), FString::Printf(TEXT("squad@%.0f,%.0f"), Nerve.Anchor.X, Nerve.Anchor.Y),
					Nerve.TeamId, TEXT("a break from here is a rout"));
			}
			Nerve.Commander.Reset();
		}
		Nerve.LastAliveCount = Alive;

		if (Alive == 0)
		{
			continue; // nothing left to have an opinion
		}

		const float Strength = static_cast<float>(Alive) / static_cast<float>(Nerve.InitialCount);

		if (!Nerve.bWithdrawing)
		{
			if (Nerve.WithdrawStrength > 0.0f && Strength <= Nerve.WithdrawStrength)
			{
				Nerve.bWithdrawing = true;
				Nerve.WithdrawStartTime = Now;

				if (UBattleLogSubsystem* const Battle = UBattleLogSubsystem::Get(this))
				{
					Battle->RecordNote(TEXT("withdraw"), FString::Printf(TEXT("squad@%.0f,%.0f"), Nerve.Anchor.X, Nerve.Anchor.Y),
						Nerve.TeamId, FString::Printf(TEXT("%d of %d left"), Alive, Nerve.InitialCount));
				}
			}
			continue;
		}

		// Back in only after it has been left alone for a while. Timing from the last casualty rather
		// than from the decision means a squad shot at while pulling back keeps pulling back instead
		// of turning round into the same fire.
		const float QuietFor = Now - FMath::Max(Nerve.LastLossTime, Nerve.WithdrawStartTime);
		if (QuietFor >= Nerve.RegroupSeconds)
		{
			Nerve.bWithdrawing = false;

			// A squad that has run does not simply go again. It looks for somebody to stand with, and
			// stands where it is if there is nobody. Resuming the old attack order instead is how a
			// beaten squad ends up walking back into the same fight alone.
			if (!TryMergeSquad(Nerve))
			{
				// Nobody to join: this is now a defended position, wherever it turned out to be.
				const FVector Rally = Nerve.Anchor;
				for (FTrackedSquadMember& Member : Members)
				{
					if (Member.SquadId == Nerve.SquadId)
					{
						Member.Task = ESquadInitialTask::Defend;
						Member.bHasObjective = false;
						ReleaseWithdrawRole(Member);
					}
				}

				Nerve.InitialCount = Alive;

				if (UBattleLogSubsystem* const Battle = UBattleLogSubsystem::Get(this))
				{
					Battle->RecordNote(TEXT("digs in"), FString::Printf(TEXT("squad@%.0f,%.0f"), Rally.X, Rally.Y),
						Nerve.TeamId, FString::Printf(TEXT("%d holding, nobody to join"), Alive));
				}
			}
		}
	}
}

bool USquadSpawnSubsystem::TryMergeSquad(USquad& Broken)
{
	// Where the survivors actually are, which after a withdrawal is the rally point, near enough.
	FVector Here = FVector::ZeroVector;
	int32 Alive = 0;
	for (const FTrackedSquadMember& Member : Members)
	{
		if (Member.SquadId != Broken.SquadId)
		{
			continue;
		}
		if (const APawn* const Pawn = Member.Pawn.Get(); Pawn && !Pawn->IsPendingKillPending())
		{
			Here += Pawn->GetActorLocation();
			++Alive;
		}
	}

	if (Alive == 0)
	{
		return false;
	}
	Here /= static_cast<float>(Alive);

	const float BrokenStrength = static_cast<float>(Alive) / FMath::Max(1, Broken.InitialCount);

	// Somebody nearby, on our side, in better shape than us. "Better shape" matters: two broken
	// squads merging would produce one broken squad and a false sense of having done something.
	USquad* Best = nullptr;
	float BestDistSq = MergeRadius * MergeRadius;

	for (USquad* const OtherPtr : Squads)
	{
		if (!OtherPtr)
		{
			continue;
		}
		USquad& Other = *OtherPtr;

		if (Other.SquadId == Broken.SquadId || Other.TeamId != Broken.TeamId || Other.LastAliveCount <= 0)
		{
			continue;
		}

		const float OtherStrength = static_cast<float>(Other.LastAliveCount) / FMath::Max(1, Other.InitialCount);
		if (OtherStrength <= BrokenStrength)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(Here, Other.Anchor);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = &Other;
		}
	}

	if (!Best)
	{
		return false;
	}

	AbsorbSquad(*Best, Broken);
	return true;
}

void USquadSpawnSubsystem::AbsorbSquad(USquad& Keeper, USquad& Absorbed)
{
	// Re-keying the members IS the merge: the survivors become part of the other squad, adopt its
	// job, and fall in at the back of its formation.
	int32 HighestSlot = -1;
	ESquadInitialTask HostTask = ESquadInitialTask::Defend;
	FVector HostObjective = FVector::ZeroVector;
	bool bHostHasObjective = false;

	for (const FTrackedSquadMember& Member : Members)
	{
		if (Member.SquadId == Keeper.SquadId)
		{
			HighestSlot = FMath::Max(HighestSlot, Member.SlotIndex);
			HostTask = Member.Task;
			HostObjective = Member.Objective;
			bHostHasObjective = Member.bHasObjective;
		}
	}

	int32 Moved = 0;
	int32 NextSlot = HighestSlot + 1;
	for (FTrackedSquadMember& Member : Members)
	{
		if (Member.SquadId != Absorbed.SquadId)
		{
			continue;
		}

		APawn* const Pawn = Member.Pawn.Get();
		if (!Pawn || Pawn->IsPendingKillPending())
		{
			continue;
		}

		ReleaseWithdrawRole(Member);
		Member.SquadId = Keeper.SquadId;
		Member.SquadOrigin = Keeper.Anchor;
		Member.Task = HostTask;
		Member.Objective = HostObjective;
		Member.bHasObjective = bHostHasObjective;
		Member.SlotIndex = NextSlot++;
		Member.LastIssuedDestination = FVector::ZeroVector;

		// The roster moves with the id. Re-keying only the tracked member used to be the whole
		// merge, because a squad was nothing but an id; now a squad owns a list of bodies, and one
		// that still lists men who answer to somebody else will try to split them off later and
		// quietly do nothing.
		Absorbed.RemoveMember(Pawn);
		Keeper.AddMember(Pawn);
		++Moved;
	}

	// An officer arriving with the newcomers takes over a squad that has lost its own.
	if (!Keeper.Commander.IsValid() && Absorbed.Commander.IsValid())
	{
		Keeper.Commander = Absorbed.Commander;
	}

	Keeper.InitialCount += Moved;
	Keeper.LastAliveCount += Moved;
	Keeper.LastMergeTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	// SurplusSince deliberately NOT reset. A merge only ever makes the roster bigger, so restarting
	// the surplus clock here is the same bug as sharing one cooldown: a point being reinforced keeps
	// pushing the moment it may release men further away, every time help arrives.
	Keeper.EnsureCommander();

	// The absorbed squad stops existing. The record stays with nothing in it, which reads as
	// "wiped" to everything else and costs one skipped iteration.
	Absorbed.InitialCount = 0;
	Absorbed.LastAliveCount = 0;
	Absorbed.Commander.Reset();
	Absorbed.Members.Reset();

	if (UBattleLogSubsystem* const Battle = UBattleLogSubsystem::Get(this))
	{
		Battle->RecordNote(TEXT("merge"), FString::Printf(TEXT("squad@%.0f,%.0f"), Absorbed.Anchor.X, Absorbed.Anchor.Y),
			Absorbed.TeamId, FString::Printf(TEXT("%d join squad@%.0f,%.0f"), Moved, Keeper.Anchor.X, Keeper.Anchor.Y));
	}

	// Its own line, not only the battle log's. The battle log drops everything while no fight is
	// marked as running, which in a Simulate pass with no player is always - so a merge left no
	// trace anywhere and the honest answer to "did they merge" was "I cannot tell". A decision this
	// system makes has to be visible on this system's own channel, whatever anybody else is doing.
	// Same mistake as logging the population brake at Verbose, one level up.
	UE_LOG(LogTemp, Log, TEXT("[WAR_DEBUG] squad %d absorbs squad %d (team %d): %d join at %s, now %d"),
		Keeper.SquadId, Absorbed.SquadId, Keeper.TeamId, Moved,
		*Keeper.ObjectiveTag.ToString(), Keeper.AliveCount());
}

void USquadSpawnSubsystem::DrawSquadDebug() const
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Life = 1.1f;
	const float Now = World->GetTimeSeconds();

	for (const USquad* const Squad : Squads)
	{
		if (!Squad)
		{
			continue;
		}

		TArray<APawn*> Pawns;
		FVector Centre;
		if (!Squad->GatherAlive(Pawns, Centre))
		{
			continue;
		}

		const FColor Colour = Squad->TeamId == 1 ? FColor(80, 140, 255) : FColor(255, 90, 80);

		// One marker per body, drawn on top of the world. This is the wallhack half.
		for (const APawn* const Pawn : Pawns)
		{
			DrawDebugSphere(World, Pawn->GetActorLocation() + FVector(0.0f, 0.0f, 120.0f),
				45.0f, 8, Colour, false, Life, SDPG_Foreground, 4.0f);
		}

		// Where it was sent, and how long until it may reorganise. The cooldown is on the label
		// because "why is that squad not splitting" is otherwise unanswerable from outside, and it
		// is the SPLIT clock for exactly that reason: the merge clock answers a different question.
		const float Cooling = FMath::Max(0.0f,
			RestructureCooldownSeconds - (Now - Squad->LastSplitTime));

		FString Text = FString::Printf(TEXT("squad %d  %d/%d"),
			Squad->SquadId, Pawns.Num(), Squad->InitialCount);
		Text += FString::Printf(TEXT("\n%s -> %s"),
			Squad->Task == ESquadTask::Advance ? TEXT("ADVANCE")
				: (Squad->Task == ESquadTask::Withdraw ? TEXT("WITHDRAW") : TEXT("HOLD")),
			Squad->ObjectiveTag.IsNone() ? TEXT("(nowhere)") : *Squad->ObjectiveTag.ToString());
		if (Cooling > 0.0f)
		{
			Text += FString::Printf(TEXT("\nreorg in %.0fs"), Cooling);
		}
		if (Squad->bWithdrawing)
		{
			Text += TEXT("\nWITHDRAWING");
		}

		DrawDebugString(World, Centre + FVector(0.0f, 0.0f, 400.0f), Text, nullptr, Colour, Life, true);

		if (Squad->bHasObjective)
		{
			DrawDebugLine(World, Centre, Squad->Objective, Colour, false, Life, SDPG_Foreground, 3.0f);
		}
	}
}

USquad* USquadSpawnSubsystem::DetachFrom(USquad& Source, int32 Count, FName NewObjectiveTag,
	const FVector& NewObjective)
{
	const int32 Alive = Source.AliveCount();
	if (Count < MinSquadSize || (Alive - Count) < MinSquadSize)
	{
		return nullptr;
	}

	UWorld* const World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	USquad* const Detached = NewObject<USquad>(this);
	Detached->SquadId = NextSquadId++;
	Detached->TeamId = Source.TeamId;
	Detached->Anchor = Source.Anchor;
	Detached->WithdrawStrength = Source.WithdrawStrength;
	Detached->RegroupSeconds = Source.RegroupSeconds;
	Detached->SetTask(ESquadTask::Advance, NewObjectiveTag, NewObjective, true);
	Detached->LastSplitTime = Now;
	Squads.Add(Detached);

	// Taken from the back of the formation. The point man and whoever is beside him are the shape
	// of the squad that stays; peeling off the tail leaves something that still looks like one.
	TArray<FTrackedSquadMember*> Candidates;
	for (FTrackedSquadMember& Member : Members)
	{
		if (Member.SquadId != Source.SquadId)
		{
			continue;
		}

		APawn* const Pawn = Member.Pawn.Get();
		if (!Pawn || Pawn->IsPendingKillPending())
		{
			continue;
		}

		// The commander stays with the parent. A detachment promotes its own; a parent that loses
		// its officer to a routine reorganisation breaks like a rout the next time it is hit.
		if (Source.Commander.Get() == Pawn)
		{
			continue;
		}

		Candidates.Add(&Member);
	}

	Candidates.Sort([](const FTrackedSquadMember& A, const FTrackedSquadMember& B)
	{
		return A.SlotIndex > B.SlotIndex;
	});

	int32 Taken = 0;
	for (FTrackedSquadMember* const Member : Candidates)
	{
		if (Taken >= Count)
		{
			break;
		}

		APawn* const Pawn = Member->Pawn.Get();
		ReleaseWithdrawRole(*Member);
		Member->SquadId = Detached->SquadId;
		Member->SquadOrigin = Detached->Anchor;
		Member->Task = ESquadInitialTask::Attack;
		Member->Objective = NewObjective;
		Member->bHasObjective = true;
		Member->SlotIndex = Taken;
		Member->LastIssuedDestination = FVector::ZeroVector;

		Source.RemoveMember(Pawn);
		Detached->AddMember(Pawn);
		++Taken;
	}

	if (Taken == 0)
	{
		Squads.Remove(Detached);
		return nullptr;
	}

	Detached->InitialCount = Taken;
	Detached->LastAliveCount = Taken;
	Detached->EnsureCommander();

	// The parent's morale is measured against what it has NOW. Without this reset it reads a
	// deliberate detachment as casualties and withdraws from a fight nobody started.
	Source.InitialCount = Source.AliveCount();
	Source.LastAliveCount = Source.InitialCount;
	Source.LastSplitTime = Now;
	Source.SurplusSince = -1.0f;

	if (UBattleLogSubsystem* const Battle = UBattleLogSubsystem::Get(this))
	{
		Battle->RecordNote(TEXT("detach"), FString::Printf(TEXT("squad@%.0f,%.0f"), Source.Anchor.X, Source.Anchor.Y),
			Source.TeamId, FString::Printf(TEXT("%d peel off for %s, %d stay"),
				Taken, *NewObjectiveTag.ToString(), Source.AliveCount()));
	}

	UE_LOG(LogTemp, Log, TEXT("[WAR_DEBUG] squad %d (team %d) detaches %d to %s, %d stay"),
		Source.SquadId, Source.TeamId, Taken, *NewObjectiveTag.ToString(), Source.AliveCount());

	return Detached;
}

USquad* USquadSpawnSubsystem::FindSquadOfPawn(const APawn* Pawn) const
{
	if (!Pawn)
	{
		return nullptr;
	}

	for (const FTrackedSquadMember& Member : Members)
	{
		if (Member.Pawn.Get() == Pawn)
		{
			return FindSquad(Member.SquadId);
		}
	}

	return nullptr;
}

FName USquadSpawnSubsystem::GetObjectiveTagOfPawn(const APawn* Pawn) const
{
	if (!Pawn)
	{
		return NAME_None;
	}

	// Linear walk of the member list. It is a few dozen entries and this is asked once per contact
	// per second, so an index keyed by pawn would cost more to keep straight than it saves.
	for (const FTrackedSquadMember& Member : Members)
	{
		if (Member.Pawn.Get() != Pawn)
		{
			continue;
		}

		const USquad* const Squad = FindSquad(Member.SquadId);
		return Squad ? Squad->ObjectiveTag : NAME_None;
	}

	return NAME_None;
}

void USquadSpawnSubsystem::TickSquadStructure()
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	URunDirectorSubsystem* const Director = URunDirectorSubsystem::GetRunDirector(World);
	if (!Director)
	{
		return;
	}

	// Empty squads are records of things that stopped existing. Dropped here rather than at the
	// moment of death: half this file walks the array, and removing from under it is how you get a
	// crash that only ever happens when somebody dies at the wrong moment.
	for (USquad* const Squad : Squads)
	{
		if (Squad)
		{
			Squad->PruneDead();
			Squad->EnsureCommander();
		}
	}
	Squads.RemoveAll([](const TObjectPtr<USquad>& Squad)
	{
		return !Squad || Squad->Members.IsEmpty();
	});

	// ---- merge: two of ours on one objective are one force ----
	for (int32 i = 0; i < Squads.Num(); ++i)
	{
		USquad* const A = Squads[i];
		// A lone operator is not a squad short of men, he is a man doing another job.
		if (!A || A->bLoneOperator || A->ObjectiveTag.IsNone()
			|| !A->CanMerge(Now, RestructureCooldownSeconds))
		{
			continue;
		}

		TArray<APawn*> APawns;
		FVector ACentre;
		if (!A->GatherAlive(APawns, ACentre))
		{
			continue;
		}

		for (int32 j = i + 1; j < Squads.Num(); ++j)
		{
			USquad* const B = Squads[j];
			if (!B || B->TeamId != A->TeamId || B->ObjectiveTag != A->ObjectiveTag
				|| B->bLoneOperator
				|| !B->CanMerge(Now, RestructureCooldownSeconds))
			{
				continue;
			}

			TArray<APawn*> BPawns;
			FVector BCentre;
			if (!B->GatherAlive(BPawns, BCentre))
			{
				continue;
			}

			// Two of ours on one objective and they still did not join: say why. Without this the
			// only observable is a merge that never happens, and every explanation for that is a
			// guess. Rare enough to log at full volume - it needs two squads sharing a tag.
			const float Gap = FVector::Dist(ACentre, BCentre);
			if (Gap > ObjectiveMergeRadius)
			{
				UE_LOG(LogTemp, Log,
					TEXT("[WAR_DEBUG] squads %d and %d both want %s but stand %.0f apart (need %.0f)"),
					A->SquadId, B->SquadId, *A->ObjectiveTag.ToString(), Gap, ObjectiveMergeRadius);
				continue;
			}

			// The bigger one plays host, so slot numbering and the commander belong to the force
			// that was already there rather than to whichever was checked first.
			if (APawns.Num() >= BPawns.Num())
			{
				AbsorbSquad(*A, *B);
			}
			else
			{
				AbsorbSquad(*B, *A);
			}
			return;   // one structure change per pass: everything else has just moved under us
		}
	}

	// ---- split: send the surplus, keep the garrison ----
	for (USquad* const Squad : Squads)
	{
		if (!Squad || Squad->bLoneOperator || Squad->ObjectiveTag.IsNone())
		{
			continue;
		}

		const int32 Alive = Squad->AliveCount();
		const int32 Demand = Director->GetDemandAt(Squad->TeamId, Squad->ObjectiveTag);
		const int32 Surplus = Alive - Demand;

		// The clock only forgets when the surplus itself is gone. Clearing it because the cooldown
		// is still running would mean a squad has to wait the cooldown AND then start counting the
		// hold from zero, which is not what either number was meant to say.
		if (Surplus < MinSquadSize)
		{
			Squad->SurplusSince = -1.0f;
			continue;
		}
		if (!Squad->CanSplit(Now, RestructureCooldownSeconds))
		{
			continue;
		}

		if (Squad->SurplusSince < 0.0f)
		{
			Squad->SurplusSince = Now;
			continue;
		}
		if ((Now - Squad->SurplusSince) < SurplusHoldSeconds)
		{
			continue;
		}

		TArray<APawn*> Pawns;
		FVector Centre;
		if (!Squad->GatherAlive(Pawns, Centre))
		{
			continue;
		}

		// Where the surplus is worth sending: the best line of the faction's own plan that is not
		// the place these men are already standing in.
		TArray<FFactionOrder> Plan;
		// The surplus is exactly what this squad can put on the road, so it is what the odds should
		// be judged on.
		Director->BuildFactionPlan(Squad->TeamId, Centre, Surplus, Plan);

		const FFactionOrder* Target = nullptr;
		for (const FFactionOrder& Line : Plan)
		{
			if (Line.bWorthIt && Line.PoiTag != Squad->ObjectiveTag)
			{
				Target = &Line;
				break;
			}
		}

		if (!Target)
		{
			// Nowhere worth going. Standing here overcrowded beats marching somewhere for the look
			// of it, so the surplus clock keeps running and the question gets asked again.
			continue;
		}

		if (DetachFrom(*Squad, Surplus, Target->PoiTag, Target->Location))
		{
			Director->NotifySortieSent(Squad->TeamId, Target->PoiTag, Surplus);
			return;   // one per pass
		}
	}
}

void USquadSpawnSubsystem::ApplyWithdrawRole(FTrackedSquadMember& Member, USquad& Nerve, float Now)
{
	AShooterNPC* const NPC = Cast<AShooterNPC>(Member.Pawn.Get());
	if (!NPC)
	{
		return;
	}

	// No commander left: nobody is covering anybody. Everyone runs, nobody shoots. That is what a
	// rout is, and it should look worse than a withdrawal, not merely faster.
	const bool bOrderly = Nerve.Commander.IsValid();

	if (!bOrderly)
	{
		Member.bHoldingToCover = false;
		Member.bCombatSuspended = true;
		NPC->SetCombatDisabled(true);
		NPC->StopShooting();

		if (UAIAccuracyComponent* const Accuracy = NPC->FindComponentByClass<UAIAccuracyComponent>())
		{
			Accuracy->SetSuppressiveFire(false);
		}
		return;
	}

	// Bounding overwatch: the squad splits in two by spawn order and the halves take turns. One half
	// runs (facing where it runs, not shooting), the other stands and fires to keep heads down.
	if (Now - Nerve.BoundStartTime >= BoundSeconds)
	{
		Nerve.BoundStartTime = Now;
		Nerve.BoundingGroup = 1 - Nerve.BoundingGroup;
	}

	const bool bMyTurnToRun = (Member.SlotIndex % 2) == Nerve.BoundingGroup;

	Member.bHoldingToCover = !bMyTurnToRun;
	Member.bCombatSuspended = bMyTurnToRun;

	NPC->SetCombatDisabled(bMyTurnToRun);
	if (bMyTurnToRun)
	{
		NPC->StopShooting();
	}

	if (UAIAccuracyComponent* const Accuracy = NPC->FindComponentByClass<UAIAccuracyComponent>())
	{
		// The ones standing still fire wide on purpose: this is fire meant to be heard, not to hit,
		// and the accuracy component already has that mode.
		Accuracy->SetSuppressiveFire(!bMyTurnToRun);
	}
}

void USquadSpawnSubsystem::ReleaseWithdrawRole(FTrackedSquadMember& Member)
{
	Member.bHoldingToCover = false;
	Member.bCombatSuspended = false;

	if (AShooterNPC* const NPC = Cast<AShooterNPC>(Member.Pawn.Get()))
	{
		NPC->SetCombatDisabled(false);

		if (UAIAccuracyComponent* const Accuracy = NPC->FindComponentByClass<UAIAccuracyComponent>())
		{
			Accuracy->SetSuppressiveFire(false);
		}
	}
}

void USquadSpawnSubsystem::CheckFightOver()
{
	if (bFightFinished || Squads.Num() == 0)
	{
		return;
	}

	TSet<uint8> SidesAlive;
	for (const USquad* const SquadPtr : Squads)
	{
		if (!SquadPtr)
		{
			continue;
		}
		const USquad& Nerve = *SquadPtr;

		if (Nerve.LastAliveCount > 0)
		{
			SidesAlive.Add(Nerve.TeamId);
		}
	}

	// One side (or nobody) still standing: there is nothing left to measure.
	if (SidesAlive.Num() > 1)
	{
		return;
	}

	bFightFinished = true;

	if (UBattleLogSubsystem* const Battle = UBattleLogSubsystem::Get(this))
	{
		const FString Reason = SidesAlive.Num() == 1
			? FString::Printf(TEXT("team %d holds the field"), *SidesAlive.CreateConstIterator())
			: FString(TEXT("nobody left"));
		Battle->FinishFight(Reason);
	}
}

bool USquadSpawnSubsystem::ResolveAdvanceGoal(const APawn* Pawn, uint8 TeamId, const FVector* Objective, FVector& OutGoal) const
{
	if (!Pawn)
	{
		return false;
	}

	const FVector From = Pawn->GetActorLocation();

	// An order outranks a sighting. This used to be the other way round and it read as the squad
	// forgetting what it was for: faction A was sent at the centre POI, saw the player standing near
	// its own spawn, retargeted onto him, and never left home. Shooting at what you can see is the
	// individual's business - their behaviour trees do it while the squad walks past - and a squad
	// that abandons its objective for the nearest contact has no objective at all.
	if (Objective)
	{
		OutGoal = *Objective;
		return true;
	}

	// No order: then the shared picture decides. What this SIDE has seen, not what exists - the
	// faction remembers every sighting any of its members reported and forgets it after twenty
	// seconds, so the tank turns back because a drone radioed a grenadier, not because the engine
	// told it where every pawn on the level is.
	if (const UFactionContactMemory* const Memory = UFactionContactMemory::Get(Pawn))
	{
		FFactionContact Contact;
		if (Memory->FindNearestContact(TeamId, From, Contact))
		{
			OutGoal = Contact.LastKnownLocation;
			return true;
		}
	}

	// Nobody has seen anybody and nobody was sent anywhere: head for whatever enemy position is
	// nearest. Not a guess about the present, a decision to go and look.
	return FindNearestHostileOrigin(From, TeamId, OutGoal);
}

void USquadSpawnSubsystem::TickSquadTasks(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	// One entry per squad, keyed by side and the point it spawned from, so two squads of the same
	// faction advance independently.
	//
	// The point man is the SLOWEST member, not the one furthest from the goal. That distinction is
	// the whole difference between a march and a shuffle: with "furthest from the goal", the drones
	// take their slots behind the tank, which makes THEM the furthest, which moves the point of the
	// formation onto a drone, which sends the tank to a slot behind it - and the squad rocks back
	// and forth forever. Slowest never changes hands, so the shape stays put.
	struct FSquadState
	{
		int32 SquadId = INDEX_NONE;
		uint8 TeamId = 0;
		FVector Origin = FVector::ZeroVector;
		FVector Goal = FVector::ZeroVector;

		TWeakObjectPtr<APawn> PointPawn;
		FVector PointLocation = FVector::ZeroVector;
		float PointDistanceToGoal = 0.0f;
		int32 PointSlotIndex = INDEX_NONE;
		float SlowestSpeed = TNumericLimits<float>::Max();

		/** Spawn order of every member, so a formation place can be worked out without depending on
		 *  who happens to be where this second. */
		TArray<int32> SlotIndices;
	};
	// Named apart from the member Squads and the member FindSquad on purpose. Both used to be
	// local-only names; now the class owns real squads, and a local called the same thing shadows
	// them silently - the compiler only complained because the types happened to differ.
	TArray<FSquadState> MarchStates;

	auto FindMarchState = [&MarchStates](int32 SquadId) -> FSquadState*
	{
		return MarchStates.FindByPredicate([SquadId](const FSquadState& Entry)
		{
			return Entry.SquadId == SquadId;
		});
	};

	// Pass one: who sets the pace in each squad.
	for (const FTrackedSquadMember& Member : Members)
	{
		APawn* const Pawn = Member.Pawn.Get();
		if (!Pawn || Pawn->IsPendingKillPending() || Member.Task != ESquadInitialTask::Attack)
		{
			continue;
		}

		FVector HostileOrigin = FVector::ZeroVector;
		if (!ResolveAdvanceGoal(Pawn, Member.TeamId, Member.bHasObjective ? &Member.Objective : nullptr, HostileOrigin))
		{
			continue;
		}

		const float DistanceToGoal = FVector::Dist(Pawn->GetActorLocation(), HostileOrigin);

		FSquadState* Entry = FindMarchState(Member.SquadId);
		if (!Entry)
		{
			Entry = &MarchStates.AddDefaulted_GetRef();
			Entry->SquadId = Member.SquadId;
			Entry->TeamId = Member.TeamId;
			Entry->Origin = Member.SquadOrigin;
		}

		Entry->Goal = HostileOrigin;
		Entry->SlotIndices.Add(Member.SlotIndex);

		// Slowest wins the point; spawn order breaks ties so two identical drones do not swap the
		// job between them every tick.
		const bool bTakesPoint = !Entry->PointPawn.IsValid()
			|| Member.BaseSpeed < Entry->SlowestSpeed
			|| (FMath::IsNearlyEqual(Member.BaseSpeed, Entry->SlowestSpeed, 1.0f) && Member.SlotIndex < Entry->PointSlotIndex);

		if (bTakesPoint && Member.BaseSpeed > 0.0f)
		{
			Entry->SlowestSpeed = Member.BaseSpeed;
			Entry->PointPawn = Pawn;
			Entry->PointSlotIndex = Member.SlotIndex;
		}

	}

	// Read the point man's position only after the whole squad has been seen: a slower member found
	// later in the list takes the job, and a position cached mid-loop would belong to whoever held it
	// at that moment. Formation places are handed out in spawn order, which is stable.
	for (FSquadState& Squad : MarchStates)
	{
		Squad.SlotIndices.Sort();

		if (const APawn* const Point = Squad.PointPawn.Get())
		{
			Squad.PointLocation = Point->GetActorLocation();
			Squad.PointDistanceToGoal = FVector::Dist(Squad.PointLocation, Squad.Goal);
		}
	}

	// Pass two: advance, or hold for the ones behind.
	for (FTrackedSquadMember& Member : Members)
	{
		APawn* Pawn = Member.Pawn.Get();
		if (!Pawn || Pawn->IsPendingKillPending() || Member.Task != ESquadInitialTask::Attack)
		{
			continue;
		}

		// Dead NPCs stop advancing
		if (AActor* Actor = Cast<AActor>(Pawn); Actor && Actor->IsActorBeingDestroyed())
		{
			continue;
		}

		AAIController* AI = Cast<AAIController>(Pawn->GetController());
		if (!AI)
		{
			Member.LastMarchDecision = TEXT("no AI controller");
			continue;
		}

		const bool bMoving = IsMemberMoving(Pawn, AI);
		if (!bMoving)
		{
			Member.bAdvancing = false;
		}

		// A broken squad marches the other way: home, to the point it formed at. Everything else in
		// this loop stays the same, formation included, so a withdrawal is an advance with its goal
		// turned round rather than a second movement system.
		USquad* const Nerve = USquadSpawnSubsystem::FindSquad(Member.SquadId);
		const bool bWithdrawing = Nerve && Nerve->bWithdrawing;

		if (bWithdrawing)
		{
			ApplyWithdrawRole(Member, *Nerve, Now);
		}
		else if (Member.bCombatSuspended)
		{
			ReleaseWithdrawRole(Member);
		}

		FVector HostileOrigin = FVector::ZeroVector;
		if (bWithdrawing)
		{
			HostileOrigin = Member.SquadOrigin;

			// The half that is covering does not move at all: it is standing and shooting so the
			// other half can run. Skipping the movement code entirely is what makes that read as
			// covering fire rather than as somebody who got stuck.
			if (Member.bHoldingToCover)
			{
				Member.LastMarchDecision = TEXT("covering");
				continue;
			}
		}
		else if (!ResolveAdvanceGoal(Pawn, Member.TeamId, Member.bHasObjective ? &Member.Objective : nullptr, HostileOrigin))
		{
			Member.LastMarchDecision = TEXT("no goal");
			continue;
		}

		const FSquadState* const Squad = FindMarchState(Member.SquadId);
		const float DistanceToGoal = FVector::Dist(Pawn->GetActorLocation(), HostileOrigin);

		// March at the pace of the slowest, rather than sprinting ahead and standing about. Stopping
		// and waiting was the first version and it read as a stutter: three drones freezing in mid
		// air every few seconds while the tank caught up. Throttling instead means they simply fly
		// slowly, in formation, which is what a squad moving together looks like.
		if (Squad && Squad->SlowestSpeed < TNumericLimits<float>::Max() && Member.BaseSpeed > 0.0f)
		{
			// Marching together is worth slowing down for. Running away is not: a squad that keeps
			// formation discipline while it breaks just dies in order, which is what six drones did
			// crossing sixty metres of open ground at the pace of the slowest of them.
			const bool bEngaged = DistanceToGoal <= AttackEngageDistance;
			const float Wanted = (bEngaged || bWithdrawing)
				? Member.BaseSpeed                                             // fighting, or running: own legs
				: FMath::Max(Squad->SlowestSpeed, MinMarchSpeed);

			// The sprint multiplies the cap as well as the wish. Capping at BaseSpeed alone would
			// mean the slow members sprint and the fast ones do not, which pulls the squad apart -
			// the exact thing the throttle above is here to prevent.
			const float Sprint = (bEngaged || bWithdrawing)
				? 1.0f
				: FMath::Max(CVarMarchSprint.GetValueOnGameThread(), 1.0f);

			const float Applied = FMath::Min(Wanted, Member.BaseSpeed) * Sprint;
			if (!FMath::IsNearlyEqual(GetMarchSpeed(Pawn), Applied, 1.0f))
			{
				SetMarchSpeed(Pawn, Applied);
			}
		}

		// Last-resort leash: something went wrong with the throttle (a detour, a teleport). Only our
		// own advance is cancelled; a pawn manoeuvring for its own reasons is left alone.
		const float AheadBy = Squad ? (Squad->PointDistanceToGoal - DistanceToGoal) : 0.0f;
		if (AheadBy > SquadCohesionRadius)
		{
			if (Member.bAdvancing && bMoving)
			{
				StopMember(Pawn, AI);
				Member.bAdvancing = false;
			}
			Member.LastMarchDecision = TEXT("ahead of the squad");
			continue;
		}

		if (Now < Member.NextMoveCheckTime)
		{
			continue; // not a decision, just not this tick
		}
		Member.NextMoveCheckTime = Now + AttackMoveReissueInterval;

		// Stopping at contact range is for an advance. A withdrawal stops only when it is home:
		// "close enough to the enemy" is precisely what it is running from.
		if (!bWithdrawing && DistanceToGoal <= AttackEngageDistance)
		{
			Member.LastMarchDecision = TEXT("in contact, fighting from here");
			continue;
		}

		if (bWithdrawing && DistanceToGoal <= AttackArriveAcceptance)
		{
			Member.LastMarchDecision = TEXT("home");
			continue;
		}

		// Where this member marches: the pace-setter walks at the goal, everyone else at their slot
		// in the formation built around it. Slots are in march space, so the shape turns with the
		// advance instead of being nailed to the world.
		FVector Destination = HostileOrigin;
		const bool bIsPointMan = !Squad || Squad->PointPawn.Get() == Pawn;

		if (!bIsPointMan)
		{
			const FVector MarchDir = (Squad->Goal - Squad->PointLocation).GetSafeNormal2D();
			if (!MarchDir.IsNearlyZero())
			{
				// Place in the formation, counted in spawn order with the point man taken out. Using
				// the raw spawn index would leave a hole in the shape whenever the point man is not
				// the first-spawned member, and squad A's slowest is its juggernaut, spawned third.
				int32 FormationIndex = 1;
				for (const int32 Slot : Squad->SlotIndices)
				{
					if (Slot == Squad->PointSlotIndex)
					{
						continue;
					}
					if (Slot == Member.SlotIndex)
					{
						break;
					}
					++FormationIndex;
				}

				const FVector Offset = FormationSlotOffset(Member.Formation, FormationIndex, Member.FormationSpacing);
				const FVector Right = FVector::CrossProduct(FVector::UpVector, MarchDir);

				Destination = Squad->PointLocation + MarchDir * Offset.X + Right * Offset.Y;
			}
		}

		// A flying member cannot be sent to a ground coordinate. Both ends of the march are on the
		// floor - the enemy spawn point, the tank at the head of the wedge - so handing that Z to a
		// drone told it to come down and skim the boards, which is what it did.
		if (const AFlyingDrone* const Drone = Cast<AFlyingDrone>(Pawn))
		{
			if (const UFlyingAIMovementComponent* const Flying = Drone->GetFlyingMovement())
			{
				const float GroundZ = (Squad && !bIsPointMan) ? Squad->PointLocation.Z : Destination.Z;
				Destination.Z = GroundZ + Flying->DefaultHoverHeight;
			}
		}

		// Refresh when the slot has TRAVELLED, not when a timer went off. A formation slot moves with
		// the squad, so an order given two seconds ago points at where the squad used to be; obeying
		// it to the letter and then waiting for the next tick is what turned the escort into a series
		// of hops.
		const bool bGoalMoved = FVector::Dist2D(Destination, Member.LastIssuedDestination) > FormationReissueDistance;
		const bool bAtSlot = FVector::Dist2D(Pawn->GetActorLocation(), Destination) <= FormationSlotAcceptance;

		// Far from the goal the order wins over whatever the pawn decided for itself; close to it,
		// it does not.
		const bool bOrderOverrides = DistanceToGoal > MarchOverrideDistance && !Member.bAdvancing;

		if (!bGoalMoved && !bOrderOverrides)
		{
			if (bMoving)
			{
				Member.LastMarchDecision = TEXT("already moving");
				continue;
			}

			if (!bIsPointMan && bAtSlot)
			{
				Member.LastMarchDecision = TEXT("in its slot");
				continue;
			}
		}

		const TCHAR* const Result = IssueMove(Pawn, AI, Destination,
			bIsPointMan ? AttackArriveAcceptance : FormationSlotAcceptance);

		Member.LastIssuedDestination = Destination;
		Member.bAdvancing = true;
		Member.LastMarchDecision = Result;
	}
}

bool USquadSpawnSubsystem::FindNearestHostileOrigin(const FVector& From, uint8 TeamId, FVector& OutOrigin) const
{
	float BestDistSq = TNumericLimits<float>::Max();
	bool bFound = false;

	// Anchors live on the squads now; there is no second array to fall out of step with them.
	for (const USquad* const Squad : Squads)
	{
		if (!Squad || Squad->TeamId == TeamId)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(From, Squad->Anchor);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			OutOrigin = Squad->Anchor;
			bFound = true;
		}
	}

	return bFound;
}

// ==================== Auto-Run hook ====================

void USquadSpawnSubsystem::RequestAutoRunFromCVar()
{
	// Reserved for callers that want to force the check early; the Tick path handles it anyway.
	bAutoRunFired = false;
}

void USquadSpawnSubsystem::ReportSquads() const
{
	for (const USquad* const SquadPtr : Squads)
	{
		if (!SquadPtr)
		{
			continue;
		}
		const USquad& Nerve = *SquadPtr;

		if (Nerve.LastAliveCount <= 0)
		{
			continue;
		}

		// Report from the first living member: what it was told to do and where it is trying to get
		// to. That pair is the whole answer to "why is this squad standing still".
		for (const FTrackedSquadMember& Member : Members)
		{
			if (Member.SquadId != Nerve.SquadId)
			{
				continue;
			}

			const APawn* const Pawn = Member.Pawn.Get();
			if (!Pawn || Pawn->IsPendingKillPending())
			{
				continue;
			}

			// Only an advancing (or withdrawing) squad has somewhere to be. A defender's "goal" was
			// computed for the line and never used, which made the report look like it was ignoring
			// its own orders.
			FVector Goal = FVector::ZeroVector;
			bool bHasGoal = false;
			if (Nerve.bWithdrawing)
			{
				Goal = Nerve.Anchor;
				bHasGoal = true;
			}
			else if (Member.Task == ESquadInitialTask::Attack)
			{
				bHasGoal = ResolveAdvanceGoal(Pawn, Member.TeamId,
					Member.bHasObjective ? &Member.Objective : nullptr, Goal);
			}

			UE_LOG(LogTemp, Warning,
				TEXT("[SQUAD_DEBUG] squad %d team=%d %s alive=%d/%d at (%.0f,%.0f) goal=%s dist=%.0f%s"),
				Nerve.SquadId, Nerve.TeamId,
				Member.Task == ESquadInitialTask::Attack ? TEXT("ATTACK") : TEXT("DEFEND"),
				Nerve.LastAliveCount, Nerve.InitialCount,
				Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y,
				bHasGoal ? *FString::Printf(TEXT("(%.0f,%.0f)"), Goal.X, Goal.Y) : TEXT("none"),
				bHasGoal ? FVector::Dist(Pawn->GetActorLocation(), Goal) : -1.0f,
				Nerve.bWithdrawing ? TEXT(" WITHDRAWING") : TEXT(""));

			// The three numbers that separate the remaining explanations for a pawn that will not
			// move: what the movement component is allowed to do (speed), what it is doing
			// (velocity), and whether path following still believes it has somewhere to be. A
			// request that was accepted and then abandoned reads as Idle here.
			const ACharacter* const AsCharacter = Cast<ACharacter>(Pawn);
			const UCharacterMovementComponent* const Movement = AsCharacter ? AsCharacter->GetCharacterMovement() : nullptr;
			const AAIController* const AI = Cast<AAIController>(Pawn->GetController());
			const UPathFollowingComponent* const PathComp = AI ? AI->GetPathFollowingComponent() : nullptr;

			const TCHAR* PathState = TEXT("no path comp");
			if (PathComp)
			{
				switch (PathComp->GetStatus())
				{
				case EPathFollowingStatus::Idle:   PathState = TEXT("Idle");   break;
				case EPathFollowingStatus::Moving: PathState = TEXT("Moving"); break;
				case EPathFollowingStatus::Paused: PathState = TEXT("Paused"); break;
				default:                           PathState = TEXT("Waiting"); break;
				}
			}

			UE_LOG(LogTemp, Warning,
				TEXT("[SQUAD_DEBUG]   squad %d first member %s: %s | vel=%.0f maxSpeed=%.0f mode=%d path=%s ctrl=%s"),
				Nerve.SquadId, *Pawn->GetName(), Member.LastMarchDecision,
				Pawn->GetVelocity().Size(),
				Movement ? Movement->MaxWalkSpeed : -1.0f,
				Movement ? static_cast<int32>(Movement->MovementMode.GetValue()) : -1,
				PathState,
				AI ? *AI->GetName() : TEXT("NONE"));

			// Told to walk, allowed to walk, walking mode, and not moving a millimetre: the only
			// thing left is that the capsule cannot leave the spot it is in. Ask what it is sharing
			// that spot with, by name, instead of guessing at level geometry from a text file.
			if (Movement && Pawn->GetVelocity().IsNearlyZero() && PathComp
				&& PathComp->GetStatus() == EPathFollowingStatus::Moving)
			{
				if (const UCapsuleComponent* const Capsule = AsCharacter->GetCapsuleComponent())
				{
					TArray<FOverlapResult> Overlaps;
					FCollisionQueryParams Params;
					Params.AddIgnoredActor(Pawn);

					GetWorld()->OverlapMultiByChannel(Overlaps, Pawn->GetActorLocation(), FQuat::Identity,
						ECC_Pawn,
						FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),
							Capsule->GetScaledCapsuleHalfHeight()),
						Params);

					FString Blockers;
					for (const FOverlapResult& Overlap : Overlaps)
					{
						if (const AActor* const Other = Overlap.GetActor())
						{
							Blockers += FString::Printf(TEXT("%s "), *Other->GetName());
						}
					}

					// Nothing in the way and still nailed down: the pawn is in a STATE that owns its
					// movement. Knockback drives position by interpolation and ignores input; capture
					// and explosion stun do the same. All three also gate shooting, which fits a
					// squad that never fired a round either.
					const AShooterNPC* const NPC = Cast<AShooterNPC>(Pawn);

					UE_LOG(LogTemp, Warning,
						TEXT("[SQUAD_DEBUG]   squad %d STUCK at (%.0f,%.0f,%.0f), overlaps: %s | knockback=%d captured=%d stunned=%d combatOff=%d cmcTick=%d maxAccel=%.0f"),
						Nerve.SquadId,
						Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y, Pawn->GetActorLocation().Z,
						Blockers.IsEmpty() ? TEXT("nothing") : *Blockers,
						NPC ? NPC->IsInKnockback() : -1,
						NPC ? NPC->IsCaptured() : -1,
						NPC ? NPC->IsStunnedByExplosion() : -1,
						NPC ? NPC->IsCombatDisabled() : -1,
						Movement->IsComponentTickEnabled() ? 1 : 0,
						Movement->GetMaxAcceleration());

					// The project's own path following overrides FollowPathSegment and, while it
					// believes a jump is in progress, does NOT call the base implementation - so
					// nothing feeds velocity to the movement component while the status stays
					// Moving. A jump flag that never got cleared looks exactly like this.
					if (const UPolarityPathFollowingComponent* const PolarityPath =
						Cast<UPolarityPathFollowingComponent>(PathComp))
					{
						UE_LOG(LogTemp, Warning, TEXT("[SQUAD_DEBUG]   squad %d jumping=%d"),
							Nerve.SquadId, PolarityPath->IsPerformingJump() ? 1 : 0);
					}

					// Ask navigation what it would actually build to the goal. "RequestSuccessful"
					// covers a PARTIAL path too, and a partial path to an unreachable goal can end
					// where it started: the order is accepted, path following reports Moving, and
					// the pawn has nowhere to step. That is indistinguishable from every other cause
					// of standing still unless the path itself is inspected.
					if (UNavigationPath* const Path = UNavigationSystemV1::FindPathToLocationSynchronously(
						GetWorld(), Pawn->GetActorLocation(), Member.LastIssuedDestination, const_cast<APawn*>(Pawn)))
					{
						UE_LOG(LogTemp, Warning,
							TEXT("[SQUAD_DEBUG]   squad %d path to (%.0f,%.0f): valid=%d partial=%d points=%d length=%.0f"),
							Nerve.SquadId,
							Member.LastIssuedDestination.X, Member.LastIssuedDestination.Y,
							Path->IsValid() ? 1 : 0,
							Path->IsPartial() ? 1 : 0,
							Path->PathPoints.Num(),
							Path->GetPathLength());
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("[SQUAD_DEBUG]   squad %d path to goal: NO PATH OBJECT"),
							Nerve.SquadId);
					}
				}
			}
			break;
		}
	}
}
