// BossStateTreeTasks.h
// StateTree Tasks and Conditions for BossCharacter.
// NOTE: dash tasks/conditions were removed in the rework. The shoot/strafe/choose-action tasks
// are added in the behavior phase. Melee is driven via ABossCharacter::StartBossMeleeAttack.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "BossCharacter.h"
#include "BossStateTreeTasks.generated.h"

class ABossCharacter;
class AActor;

//////////////////////////////////////////////////////////////////
// TASK: Boss Melee Attack
// Picks lunge / in-place montage by distance (handled in StartBossMeleeAttack).
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossMeleeAttackInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(meta = (DisplayName = "Boss Melee Attack", Category = "Boss"))
struct POLARITY_API FStateTreeBossMeleeAttackTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossMeleeAttackInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Boss Enter Finisher Phase
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossEnterFinisherInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(meta = (DisplayName = "Boss Enter Finisher Phase", Category = "Boss"))
struct POLARITY_API FStateTreeBossEnterFinisherTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossEnterFinisherInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Boss Set Phase
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossSetPhaseInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	EBossPhase NewPhase = EBossPhase::Ground;
};

USTRUCT(meta = (DisplayName = "Boss Set Phase", Category = "Boss"))
struct POLARITY_API FStateTreeBossSetPhaseTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossSetPhaseInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITIONS
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Phase Is
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossPhaseIsInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	EBossPhase ExpectedPhase = EBossPhase::Ground;
};

USTRUCT(DisplayName = "Boss Phase Is", Category = "Boss")
struct POLARITY_API FStateTreeBossPhaseIsCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossPhaseIsInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Can Melee Attack (wraps inherited CanAttack)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossCanMeleeInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(DisplayName = "Boss Can Melee Attack", Category = "Boss")
struct POLARITY_API FStateTreeBossCanMeleeCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossCanMeleeInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Is In Melee Range (wraps inherited IsTargetInAttackRange)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossInMeleeRangeInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(DisplayName = "Boss Is In Melee Range", Category = "Boss")
struct POLARITY_API FStateTreeBossInMeleeRangeCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossInMeleeRangeInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Is In Finisher Phase
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossInFinisherInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(DisplayName = "Boss Is In Finisher Phase", Category = "Boss")
struct POLARITY_API FStateTreeBossInFinisherCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossInFinisherInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Boss Choose Action (weighted Shoot / Melee; forced Melee while disarmed)
// Run this between strafes; branch afterwards on the "Boss Pending Action Is" condition.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossChooseActionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(meta = (DisplayName = "Boss Choose Action", Category = "Boss"))
struct POLARITY_API FStateTreeBossChooseActionTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossChooseActionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Boss Shoot (single ranged burst)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossShootInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	/** Safety cap — end the task even if the burst never reports complete (e.g. out of ammo). */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.5"))
	float MaxShootDuration = 5.0f;

	UPROPERTY()
	float ElapsedTime = 0.0f;
};

USTRUCT(meta = (DisplayName = "Boss Shoot", Category = "Boss"))
struct POLARITY_API FStateTreeBossShootTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossShootInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Boss Strafe (Sekiro/DS orbit around the player for a duration)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossStrafeInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	UPROPERTY()
	float ElapsedTime = 0.0f;

	UPROPERTY()
	float RepathTimer = 0.0f;

	UPROPERTY()
	float Direction = 1.0f;

	UPROPERTY()
	float Duration = 2.5f;
};

USTRUCT(meta = (DisplayName = "Boss Strafe", Category = "Boss"))
struct POLARITY_API FStateTreeBossStrafeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossStrafeInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Is Disarmed (no ranged weapon)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossIsDisarmedInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(DisplayName = "Boss Is Disarmed", Category = "Boss")
struct POLARITY_API FStateTreeBossIsDisarmedCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossIsDisarmedInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Pending Action Is (branch after Choose Action)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossPendingActionIsInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	EBossAction ExpectedAction = EBossAction::Shoot;
};

USTRUCT(DisplayName = "Boss Pending Action Is", Category = "Boss")
struct POLARITY_API FStateTreeBossPendingActionIsCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossPendingActionIsInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Boss Approach Target
// Runs up to the player (nav MoveTo) until within AttackRange, then Succeeds — so the melee
// attack always lunges from a controlled distance instead of across the arena.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossApproachInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	/** Re-issue MoveTo this often (s) as a safety net against aborted/stuck paths. */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.1"))
	float RepathInterval = 0.5f;

	UPROPERTY()
	float RepathTimer = 0.0f;
};

USTRUCT(meta = (DisplayName = "Boss Approach Target", Category = "Boss"))
struct POLARITY_API FStateTreeBossApproachTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossApproachInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};
