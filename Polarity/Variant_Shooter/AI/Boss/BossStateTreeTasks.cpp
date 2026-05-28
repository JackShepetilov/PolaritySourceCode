// BossStateTreeTasks.cpp
// StateTree Tasks and Conditions implementation for BossCharacter.

#include "BossStateTreeTasks.h"
#include "BossCharacter.h"
#include "StateTreeExecutionContext.h"
#include "GameFramework/CharacterMovementComponent.h"

//////////////////////////////////////////////////////////////////
// TASK: Boss Melee Attack
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossMeleeAttackTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.Boss->CanAttack())
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.Boss->StartBossMeleeAttack(InstanceData.Target);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeBossMeleeAttackTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.Boss->IsAttacking())
	{
		return EStateTreeRunStatus::Running;
	}

	return EStateTreeRunStatus::Succeeded;
}

void FStateTreeBossMeleeAttackTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
}

#if WITH_EDITOR
FText FStateTreeBossMeleeAttackTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss: Melee Attack (lunge / in-place)"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Boss Enter Finisher Phase
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossEnterFinisherTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.Boss->EnterFinisherPhase();
	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FStateTreeBossEnterFinisherTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss: Enter Finisher Phase"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Boss Set Phase
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossSetPhaseTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.Boss->SetPhase(InstanceData.NewPhase);
	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FStateTreeBossSetPhaseTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	if (Data)
	{
		const FString PhaseName = (Data->NewPhase == EBossPhase::Finisher) ? TEXT("Finisher") : TEXT("Ground");
		return FText::FromString(FString::Printf(TEXT("Boss: Set Phase to %s"), *PhaseName));
	}
	return FText::FromString(TEXT("Boss: Set Phase"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITIONS
//////////////////////////////////////////////////////////////////

bool FStateTreeBossPhaseIsCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	return InstanceData.Boss && InstanceData.Boss->GetCurrentPhase() == InstanceData.ExpectedPhase;
}

#if WITH_EDITOR
FText FStateTreeBossPhaseIsCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	if (Data)
	{
		const FString PhaseName = (Data->ExpectedPhase == EBossPhase::Finisher) ? TEXT("Finisher") : TEXT("Ground");
		return FText::FromString(FString::Printf(TEXT("Boss Phase is %s"), *PhaseName));
	}
	return FText::FromString(TEXT("Boss Phase is"));
}
#endif

bool FStateTreeBossCanMeleeCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	return InstanceData.Boss && InstanceData.Boss->CanAttack();
}

#if WITH_EDITOR
FText FStateTreeBossCanMeleeCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Can Melee Attack"));
}
#endif

bool FStateTreeBossInMeleeRangeCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	return InstanceData.Boss && InstanceData.Target && InstanceData.Boss->IsTargetInAttackRange(InstanceData.Target);
}

#if WITH_EDITOR
FText FStateTreeBossInMeleeRangeCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Is In Melee Range"));
}
#endif

bool FStateTreeBossInFinisherCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	return InstanceData.Boss && InstanceData.Boss->IsInFinisherPhase();
}

#if WITH_EDITOR
FText FStateTreeBossInFinisherCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Is In Finisher Phase"));
}
#endif
