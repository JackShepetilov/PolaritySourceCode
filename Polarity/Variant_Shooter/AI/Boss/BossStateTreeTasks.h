// BossStateTreeTasks.h
// StateTree Tasks and Conditions for BossCharacter

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "BossCharacter.h"
#include "BossStateTreeTasks.generated.h"

class ABossCharacter;
class AActor;

//////////////////////////////////////////////////////////////////
// TASK: Boss Approach Dash
// Dashes TOWARDS the player to close distance
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossApproachDashInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(meta = (DisplayName = "Boss Approach Dash", Category = "Boss"))
struct POLARITY_API FStateTreeBossApproachDashTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossApproachDashInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Boss Circle Dash
// Dashes AROUND the player at current distance
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossCircleDashInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(meta = (DisplayName = "Boss Circle Dash", Category = "Boss"))
struct POLARITY_API FStateTreeBossCircleDashTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossCircleDashInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Boss Melee Attack
// Executes single melee attack after dash
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

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

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

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

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

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

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

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Can Dash
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossCanDashInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(DisplayName = "Boss Can Dash", Category = "Boss")
struct POLARITY_API FStateTreeBossCanDashCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossCanDashInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Can Melee Attack
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

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Is Dashing
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossIsDashingInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(DisplayName = "Boss Is Dashing", Category = "Boss")
struct POLARITY_API FStateTreeBossIsDashingCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossIsDashingInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Is In Melee Range
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

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Target Is Far (needs approach dash)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossTargetIsFarInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(DisplayName = "Boss Target Is Far", Category = "Boss")
struct POLARITY_API FStateTreeBossTargetIsFarCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossTargetIsFarInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Target Is Close (no approach needed)
// NOTE: StateTree does NOT support condition inversion, so we keep both Far and Close.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossTargetIsCloseInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(DisplayName = "Boss Target Is Close", Category = "Boss")
struct POLARITY_API FStateTreeBossTargetIsCloseCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossTargetIsCloseInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

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

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Is On Ground (Walking)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeBossIsOnGroundInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ABossCharacter> Boss;
};

USTRUCT(DisplayName = "Boss Is On Ground", Category = "Boss")
struct POLARITY_API FStateTreeBossIsOnGroundCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeBossIsOnGroundInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};
