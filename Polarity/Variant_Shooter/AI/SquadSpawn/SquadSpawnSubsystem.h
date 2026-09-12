// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SquadLoadout.h"
#include "Squad.h"
#include "SquadSpawnSubsystem.generated.h"

class APawn;
class ASquadSpawnPoint;
class USquadScenario;

/**
 * Owns everything the squad spawn system has brought into the world: the spawn points registry,
 * the spawned-member list, the Defend/Attack task loop and the auto-run hook for headless runs.
 *
 * Console entry points live on AShooterPlayerController (Exec functions) and route here.
 *
 * Everything spawns on authority only; members live and die like any other NPC.
 */
UCLASS()
class POLARITY_API USquadSpawnSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:

	// ==================== USubsystem ====================

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(USquadSpawnSubsystem, STATGROUP_Tickables);
	}

	// ==================== Registry ====================

	/** Spawn points register themselves on BeginPlay so tags resolve from anywhere */
	void RegisterSpawnPoint(ASquadSpawnPoint* Point);
	void UnregisterSpawnPoint(ASquadSpawnPoint* Point);

	// ==================== Spawning ====================

	/** Spawn a squad at every point matching Tag. Returns total members spawned. */
	int32 SpawnAtTag(FName Tag, USquadLoadout* Loadout);

	/** Run a whole scenario: every entry spawns its loadout at its tagged points. Returns
	 *  members spawned, or -1 if nothing matched. */
	int32 RunScenario(USquadScenario* Scenario);

	/** Destroy every member the system spawned. Returns how many were removed. */
	int32 ClearAll();

	/** Core spawner used by points, commands and scenarios.
	 *
	 *  MaxMembers caps how many of the loadout actually walk out; 0 is the whole thing. A cut-down
	 *  squad rather than no squad is what keeps a population limit from turning the war off: at the
	 *  ceiling a headquarters would otherwise sit silent until somebody died, and the war would go
	 *  quiet exactly when it was busiest. The commander survives the cut - a squad with no one in
	 *  charge routs instead of withdrawing, which is a different squad, not a smaller one.
	 *
	 *  ObjectiveTag names the place the squad is FOR. Without it the squad has a destination but no
	 *  idea what it is standing on, and the structure tick cannot ask how many are wanted there,
	 *  which is the whole basis of splitting a surplus off. */
	int32 SpawnSquadMembers(const FVector& Origin, float ScatterRadius, USquadLoadout* Loadout,
		const FVector* Objective = nullptr, int32 MaxMembers = 0, FName ObjectiveTag = NAME_None,
		USquad** OutSquad = nullptr);

	/** Pawns of one side this system spawned and that are still alive.
	 *
	 *  Counted off the tracked list rather than by walking the world: garrisons come through the
	 *  same spawner as sorties, so this is the whole standing strength of a faction, which is
	 *  exactly what a population ceiling has to be measured against. */
	int32 CountAliveOnTeam(uint8 TeamId) const;

	// ==================== Auto-Run (headless / unattended runs) ====================

	/** Set once via the `squad.AutoRunScenario` console variable: a content path to a
	 *  USquadScenario that runs automatically shortly after the world is ready. Empty = off. */
	void RequestAutoRunFromCVar();

	/** Where this pawn is going, by the tag of the point its squad was sent to. NAME_None for
	 *  anybody who is not in a squad - a player, a lone garrison spawn, a pawn already forgotten.
	 *
	 *  Exists for the war director: when a scout reports a contact, the useful question is not
	 *  "where is he standing" but "which of my points is he walking at", and the squad already
	 *  carries that answer. Reading it is cheating in the sense that no observer could know it,
	 *  and that is deliberate: guessing the destination from a heading was the alternative, and a
	 *  wrong guess sends a garrison to the wrong place, which reads as stupidity rather than fog. */
	FName GetObjectiveTagOfPawn(const APawn* Pawn) const;

	/** The squad this pawn belongs to, or null.
	 *
	 *  Public because a lone operator's squad is the only place his own tasks can keep anything.
	 *  StateTree bindings between two tasks of one state cannot be made from Python at all, so the
	 *  squad does that job instead: ObjectiveTag is the point he is watching, Objective is where he
	 *  is walking right now, and each task reads what the previous one left there. */
	USquad* FindSquadOfPawn(const APawn* Pawn) const;

private:

	// ==================== Internals ====================

	ASquadSpawnPoint* FindFirstPointByTag(FName Tag) const;

	/** Per-tick v1 squad tasks: Attack members advance toward the nearest hostile squad origin */
	void TickSquadTasks(float DeltaTime);

	/** Nearest tracked squad origin belonging to a different team */
	bool FindNearestHostileOrigin(const FVector& From, uint8 TeamId, FVector& OutOrigin) const;

	/** All point actors currently registered with a given tag */
	TArray<ASquadSpawnPoint*> GetPointsByTag(FName Tag) const;

	/** One line per spawned member */
	struct FTrackedSquadMember
	{
		TWeakObjectPtr<APawn> Pawn;

		/** Which squad this member belongs to.
		 *
		 *  An identity of its own, NOT the pair (team, spawn point) it used to be: a scenario can put
		 *  two squads of the same side on one point - a garrison and the force it sends out - and
		 *  keying by position silently welded those into a single eleven-man squad with one morale
		 *  and one commander, which is also why the survivors of one could never merge into the
		 *  other. They were already the same squad. */
		int32 SquadId = INDEX_NONE;

		uint8 TeamId = 1;
		ESquadInitialTask Task = ESquadInitialTask::Defend;
		FVector SquadOrigin = FVector::ZeroVector;
		float NextMoveCheckTime = 0.0f;

		/** True while this member is running an advance order THIS system gave it. Without it the
		 *  cohesion hold would cancel whatever else was moving the pawn (its own cover move, a
		 *  reposition), which is the same mistake that kept the tank parked. */
		bool bAdvancing = false;

		/** Place in the marching formation. Handed out at spawn and never reshuffled: a squad whose
		 *  members swap sides every time somebody dies looks like a bug, not like tactics. */
		int32 SlotIndex = 0;

		ESquadFormation Formation = ESquadFormation::Wedge;
		float FormationSpacing = 500.0f;

		/** Top speed this pawn was built with, remembered before the march ever throttles it. The
		 *  throttle only ever lowers speed, so re-reading the live value would ratchet the squad
		 *  down to a crawl over a few ticks. */
		float BaseSpeed = 0.0f;

		/** Where this member was last sent. A formation slot travels with the squad, so the order has
		 *  to be refreshed when the slot has moved - not on a timer, which is what made the escort
		 *  advance in visible hops. */
		FVector LastIssuedDestination = FVector::ZeroVector;

		/** Where this squad was told to go when it has no enemy in sight. Zero = nearest enemy. */
		FVector Objective = FVector::ZeroVector;
		bool bHasObjective = false;

		/** Standing still and shooting so somebody else can run. Set only during an orderly
		 *  withdrawal. */
		bool bHoldingToCover = false;

		/** Fire is switched off on this member (routing, or bounding to the next position). Kept so
		 *  the flags can be handed back exactly once, when the withdrawal ends. */
		bool bCombatSuspended = false;

		/** Why the march loop did what it did on the last pass, for the heartbeat.
		 *
		 *  Every reason to NOT issue an order is a silent `continue`, and from outside they all look
		 *  identical: a squad standing still. A literal is enough - these are compile-time strings,
		 *  never built at runtime. */
		const TCHAR* LastMarchDecision = TEXT("not ticked");
	};


	/** Where an attacking member should head: the nearest living enemy if there is one, otherwise the
	 *  enemy squad's spawn point. Marching at the spawn point alone is why the survivors of a fight
	 *  stood around at an empty corner of the map while an enemy was still alive somewhere else. */
	bool ResolveAdvanceGoal(const APawn* Pawn, uint8 TeamId, const FVector* Objective, FVector& OutGoal) const;

	/** Every squad alive on the map.
	 *
	 * Replaces the parallel FSquadNerve / FSquadOrigin arrays this used to keep. Those held exactly
	 * the same data keyed by the same SquadId, but with nothing owning a squad there was nowhere to
	 * put the two decisions the faction war needs most: detaching a surplus, and folding a
	 * reinforcement into the force it came to help. USquad owns roster, task and nerve; the per-pawn
	 * march state stays on FTrackedSquadMember, where it belongs.
	 *
	 * UPROPERTY because a subsystem is a UObject and this is what keeps the squads from being
	 * collected out from under the tick. */
	UPROPERTY()
	TArray<TObjectPtr<USquad>> Squads;

	/** Put a member into its role for this moment of the withdrawal: running, or covering the ones
	 *  who run. Answers the contradiction that sprinting faces where you go and shooting faces the
	 *  enemy, by never asking one pawn to do both. Declared after both structs on purpose: naming
	 *  them with a leading `struct` up here would declare two new global types instead. */
	void ApplyWithdrawRole(FTrackedSquadMember& Member, USquad& Squad, float Now);

	/** Give a member its combat back after the withdrawal */
	void ReleaseWithdrawRole(FTrackedSquadMember& Member);

	/** Fold a broken squad's survivors into a healthier friendly squad nearby. False when there is
	 *  nobody worth joining, which means the survivors hold where they stand instead. */
	bool TryMergeSquad(USquad& Broken);

	/** Move every living member of one squad into another. The re-keying IS the merge; the absorbed
	 *  squad is left empty. Used by the broken-squad path and by the structure tick, which needs the
	 *  same operation for an entirely different reason: two squads standing on one objective are one
	 *  force, and a reinforcement that stays separate gets beaten separately. */
	void AbsorbSquad(USquad& Keeper, USquad& Absorbed);

	/** Peel Count members off a squad as a new one with its own commander and its own orders.
	 *
	 *  The mechanic the whole layer exists for. A point holding twenty when it needs six should send
	 *  fourteen somewhere - not move all twenty and leave the place empty, which is the same crowd
	 *  standing somewhere else. Never takes the commander, and never leaves either half below a
	 *  workable size: two men with nobody in charge is not a squad, it is a casualty report. */
	USquad* DetachFrom(USquad& Source, int32 Count, FName NewObjectiveTag, const FVector& NewObjective);

	/** Slow loop: who is overcrowded, who should join whom. Runs on its own timer, not per frame -
	 *  these are decisions measured in tens of seconds. */
	void TickSquadStructure();

	/** Every squad in the world, through walls, behind `polarity.squads.draw 1`. */
	void DrawSquadDebug() const;

	float StructureTimer = 0.0f;

	USquad* FindSquad(int32 SquadId) const;

	/** Handed out per spawned squad, never reused within a run */
	int32 NextSquadId = 0;

	/** Update strength, decide withdraw and regroup, report both to the battle log */
	void TickSquadNerve();

	/** One line per living squad every couple of seconds: what it was told to do, where it is, where
	 *  it is trying to go. The answer to "why did nobody advance", which is invisible from outside. */
	void ReportSquads() const;

	float SquadReportTimer = 0.0f;

	/** True once a side has nobody standing while another still has: end of fight */
	void CheckFightOver();

	bool bFightFinished = false;

	TArray<TWeakObjectPtr<ASquadSpawnPoint>> SpawnPoints;
	TArray<FTrackedSquadMember> Members;

	// Squad origins used to live in their own array; they are USquad::Anchor now.

	/** Auto-run bookkeeping: fires once after the world has settled */
	float ElapsedSinceStart = 0.0f;
	bool bAutoRunFired = false;
};
