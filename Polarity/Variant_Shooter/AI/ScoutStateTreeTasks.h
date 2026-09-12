// ScoutStateTreeTasks.h
// Tasks and conditions for the two units that work alone: the scout drone and the sniper.
//
// What they are FOR: a faction learns about its enemies only where its own men are standing, and
// its men stand on points. Columns cross the ground between points unseen, so the war plan is
// always one arrival behind. These two are the eyes that fix that, and the reporting itself is
// free - every sighting already lands in UFactionContactMemory, and the director now reads it
// (URunDirectorSubsystem::UpdateIntelFromContacts). Nothing here has to "report"; it only has to
// put a pair of eyes somewhere worth looking from, and keep them alive.
//
// WHERE THE STATE LIVES: in the operator's own USquad, not in bindings.
//
// StateTree can bind one task's output to another's input in the editor, but the Python service
// exposes only three kinds of binding - context, root parameter, and global task - and a global
// task that ever returns Succeeded kills the whole tree the moment it starts (Docs/Gotchas/
// AI_StateTree.md, paid for by the tank). So these tasks pass work to each other through the squad
// the operator was spawned into:
//
//   ObjectiveTag  - the point he is watching. Set once, when the post is chosen.
//   Objective     - where he is walking or flying RIGHT NOW. Rewritten as he refines it.
//
// One task writes, the next reads. No bindings to lose, and the values are visible in any squad
// dump when something goes wrong.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "ScoutStateTreeTasks.generated.h"

class AActor;
class AAIController;
class AFlyingDrone;

//////////////////////////////////////////////////////////////////
// TASK: Pick Observation Post
// Asks the war director where this side is blind, and writes the answer onto the squad.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreePickObservationPostInstanceData
{
	GENERATED_BODY()

	/** Who is asking. His team decides whose blind spots we are talking about, and his squad is
	 *  where the answer is left. */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AActor> Actor;
};

USTRUCT(meta = (DisplayName = "Pick Observation Post", Category = "Scout"))
struct POLARITY_API FStateTreePickObservationPostTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreePickObservationPostInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

//////////////////////////////////////////////////////////////////
// TASK: Drone Fly To Post
// The stock drone flight task only goes somewhere random. An observer goes where he was sent.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneFlyToPostInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;
};

USTRUCT(meta = (DisplayName = "Drone Fly To Post", Category = "Scout"))
struct POLARITY_API FStateTreeDroneFlyToPostTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneFlyToPostInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

//////////////////////////////////////////////////////////////////
// TASK: Move To Post
// Plain walk to wherever the squad currently says. Written rather than borrowed because every
// existing movement task in this project decides its own destination.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeMoveToPostInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AActor> Actor;

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "10.0"))
	float AcceptanceRadius = 150.0f;
};

USTRUCT(meta = (DisplayName = "Move To Post", Category = "Scout"))
struct POLARITY_API FStateTreeMoveToPostTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeMoveToPostInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

//////////////////////////////////////////////////////////////////
// TASK: Pick Sniper Nest
// A good nest is one you can see the target from and almost nothing else can see you from.
// Refines the rough post into an actual firing position and writes it back onto the squad.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreePickSniperNestInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AActor> Actor;

	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "1000.0"))
	float MinRange = 6000.0f;

	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "1000.0"))
	float MaxRange = 14000.0f;

	/** How many spots to try. Cheap enough at a dozen; this runs when he relocates, not per frame. */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "4", ClampMax = "48"))
	int32 Candidates = 16;
};

USTRUCT(meta = (DisplayName = "Pick Sniper Nest", Category = "Scout"))
struct POLARITY_API FStateTreePickSniperNestTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreePickSniperNestInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

//////////////////////////////////////////////////////////////////
// CONDITION: Route Is Clear
// Is there anybody we already know about sitting on the way there?
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeRouteIsClearInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AActor> Actor;

	/** How wide the road counts as. A contact further off the line than this is somebody else's
	 *  problem. */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "100.0"))
	float Corridor = 4000.0f;
};

USTRUCT(DisplayName = "Route Is Clear", Category = "Scout")
struct POLARITY_API FStateTreeRouteIsClearCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeRouteIsClearInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

//////////////////////////////////////////////////////////////////
// CONDITION: Shot Would Matter
// The whole character of the sniper, in one question.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeShotWouldMatterInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AActor> Actor;

	/** Who he is thinking about shooting. */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	TObjectPtr<AActor> Target;

	/** How lopsided a fight can be and still be worth a bullet. Beyond this in either direction the
	 *  shot changes nothing: they were going to win anyway, or they were not going to. */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "1", ClampMax = "10"))
	int32 CloseFightMargin = 2;

	/** How near a player another enemy has to be before the player counts as busy. */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "200.0"))
	float PlayerCompanyRadius = 3500.0f;
};

USTRUCT(DisplayName = "Shot Would Matter", Category = "Scout")
struct POLARITY_API FStateTreeShotWouldMatterCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeShotWouldMatterInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

//////////////////////////////////////////////////////////////////
// CONDITION: Position Compromised
// Somebody is close enough to see him. Time to leave, and if leaving fails, to stop pretending.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreePositionCompromisedInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AActor> Actor;

	/** Inside this, with a clear line, he is found. A sniper is not compromised by being shot at
	 *  from far away - that happens - he is compromised by somebody walking up the hill. */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "200.0"))
	float Radius = 3000.0f;

	/** Require an unobstructed line before calling it. Off means proximity alone is enough, which
	 *  is the jumpier and more paranoid version. */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bRequireLineOfSight = true;
};

USTRUCT(DisplayName = "Position Compromised", Category = "Scout")
struct POLARITY_API FStateTreePositionCompromisedCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreePositionCompromisedInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
