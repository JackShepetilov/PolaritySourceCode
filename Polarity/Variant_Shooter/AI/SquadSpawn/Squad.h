// Squad.h
// The operational unit of the faction war: a roster, a task, and a nerve.
//
// Layer 1 of the three in Docs/Faction_War_Master_Plan_2026-08-25.md §5. Above it the director
// decides WHERE a faction wants force and HOW MUCH; below it the StateTree tasks and
// AICombatCoordinator decide how an individual fights. Neither of those changes here: a squad hands
// out objectives and holds morale, and never touches a trigger.
//
// Why an object rather than the three parallel arrays this replaces. The squad already existed as
// an identity - FTrackedSquadMember carried a SquadId and FSquadNerve was keyed by it - but nothing
// owned it, so the two things the war needs most had nowhere to live:
//
//   SPLIT. A point holding twenty when it needs six should send fourteen somewhere, not move all
//   twenty and leave the place empty. That is a decision about a roster, and a roster spread across
//   arrays keyed by an int has no place to make it.
//
//   MERGE. Two squads standing on the same objective are one force. Reinforcement that arrives as a
//   separate squad gets beaten separately, which is how a faction feeds a fight piecemeal.
//
// The per-pawn march state (formation slot, throttled speed, last issued destination) deliberately
// stays in FTrackedSquadMember. That is tactical, it belongs to layer 2, and moving it here would
// make this class the thing it exists to avoid: one object that knows everything.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Variant_Shooter/AI/SquadSpawn/SquadLoadout.h"
#include "Squad.generated.h"

class APawn;

/** What a squad is doing right now. Deliberately the primitives from the master plan §2 and not a
 *  wider vocabulary: everything else in that document is a COMBINATION of these plus a destination
 *  (an intercept is Advance to an ambush point, a reinforcement is Advance then Merge). */
UENUM(BlueprintType)
enum class ESquadTask : uint8
{
	/** Stand on the objective and fight whoever comes. */
	Hold UMETA(DisplayName = "Hold"),

	/** March to the objective, engaging what gets in the way. */
	Advance UMETA(DisplayName = "Advance"),

	/** Broken: pulling back to the anchor, covering each other. Nerve owns entry and exit. */
	Withdraw UMETA(DisplayName = "Withdraw")
};

UCLASS()
class POLARITY_API USquad : public UObject
{
	GENERATED_BODY()

public:

	// ==================== Identity ====================

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	int32 SquadId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	uint8 TeamId = 1;

	// ==================== Task ====================

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	ESquadTask Task = ESquadTask::Hold;

	/** The place this task is about, when it is about a place. Kept as a tag as well as a position
	 *  because a point can be streamed out and still be the thing the squad was sent to. */
	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	FName ObjectiveTag;

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	FVector Objective = FVector::ZeroVector;

	/** Works alone on purpose: a scout drone or a sniper, not a squad that happens to be small.
	 *
	 *  Everything the squad layer does to a short roster is wrong for these two. The merge would
	 *  weld a scout into the first garrison he walked past - and walking past garrisons is his job;
	 *  the surplus split would count him as spare men and post him somewhere; the nerve would call
	 *  a one-man squad broken the moment it took a hit.
	 *
	 *  So this is a set of exemptions, not a role. The squad layer keeps track of him and otherwise
	 *  leaves him alone; what he actually does lives in his own StateTree. */
	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	bool bLoneOperator = false;

	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	bool bHasObjective = false;

	/** Where it formed, and where a withdrawal runs to. */
	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	FVector Anchor = FVector::ZeroVector;

	// ==================== Roster ====================

	/** Weak on purpose: a squad must never be the reason a dead pawn stays loaded. Everything that
	 *  reads this filters through AliveCount / GatherAlive rather than trusting Num(). */
	UPROPERTY()
	TArray<TWeakObjectPtr<APawn>> Members;

	/** Whoever is in charge. Its survival decides HOW the squad breaks - an orderly withdrawal with
	 *  covering fire, or a rout with the weapons down - which is the whole reason to have one. */
	UPROPERTY()
	TWeakObjectPtr<APawn> Commander;

	/** True once somebody the loadout actually marked has taken command, so a later unmarked member
	 *  cannot quietly replace them. A promotion after a split clears it deliberately: the new squad
	 *  has no marked commander and the best available body gets the job. */
	UPROPERTY()
	bool bCommanderWasChosen = false;

	// ==================== Nerve ====================

	/** How many it started with. Reset on a merge and on a dig-in, because morale is about what has
	 *  happened to THIS force since it last had a reason to feel whole. */
	UPROPERTY(BlueprintReadOnly, Category = "Squad|Nerve")
	int32 InitialCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Squad|Nerve")
	float WithdrawStrength = 0.34f;

	UPROPERTY(BlueprintReadOnly, Category = "Squad|Nerve")
	float RegroupSeconds = 20.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Squad|Nerve")
	bool bWithdrawing = false;

	/** Which half is running right now; the other half is shooting to keep heads down. */
	UPROPERTY()
	int32 BoundingGroup = 0;

	UPROPERTY()
	float BoundStartTime = 0.0f;

	UPROPERTY()
	float WithdrawStartTime = 0.0f;

	/** World time it last lost somebody: regrouping restarts on every fresh casualty. */
	UPROPERTY()
	float LastLossTime = 0.0f;

	/** Alive count seen on the previous tick, to notice losses without a delegate. */
	UPROPERTY()
	int32 LastAliveCount = 0;

	// ==================== Structure ====================

	/** World time this squad last SPLIT, and last MERGED. Two clocks, not one.
	 *
	 *  They were one field, and that was a real bug: a place receiving reinforcements merges every
	 *  time one arrives, each merge reset the shared clock, and so a point under a stream of help
	 *  could never release its surplus. Measured on the bench 2026-09-06: six drones sitting on the
	 *  centre that wanted three, while a fight they could have joined was being lost to the west.
	 *
	 *  Absorbing help and sending a surplus away are different decisions and must not share a lock.
	 *  What still has to be guarded is oscillation, and that is what each clock does on its own:
	 *  a fresh detachment is not re-absorbed, and a squad does not split twice in a breath. */
	UPROPERTY()
	float LastSplitTime = -1000.0f;

	UPROPERTY()
	float LastMergeTime = -1000.0f;

	/** World time this squad first noticed it had more people than its objective wants. A surplus
	 *  has to PERSIST before anybody acts on it, for the same reason. */
	UPROPERTY()
	float SurplusSince = -1.0f;

	// ==================== Queries ====================

	/** Members still alive. The only honest size of a squad; Members.Num() counts corpses. */
	int32 AliveCount() const;

	/** Alive members into OutPawns, and their average position. False when there is nobody left. */
	bool GatherAlive(TArray<APawn*>& OutPawns, FVector& OutCentre) const;

	/** Fraction still standing, 0..1, against what it last considered full strength. */
	float Strength() const;

	bool IsDead() const { return AliveCount() == 0; }

	/** May send a surplus away. Only its own last split holds it back: help that arrived a moment
	 *  ago is a reason to have MORE to send, not a reason to sit on it. */
	bool CanSplit(float Now, float CooldownSeconds) const;

	/** May absorb another squad. Blocked by a recent merge, and also by a recent split - otherwise
	 *  a detachment that has not left yet gets swallowed back by the squad it just left. */
	bool CanMerge(float Now, float CooldownSeconds) const;

	// ==================== Orders ====================

	void SetTask(ESquadTask NewTask, FName NewObjectiveTag, const FVector& NewObjective, bool bHasNewObjective);

	/** Somebody has to lead. Picks the marked commander if one is alive, otherwise the first body
	 *  that is. Called after every loss and every structure change: a squad whose commander died in
	 *  the split has to notice before it needs him. */
	void EnsureCommander();

	void AddMember(APawn* Pawn);
	void RemoveMember(APawn* Pawn);

	/** Drop dead weak pointers. Called by the subsystem's tick, not on every query. */
	void PruneDead();
};
