// BossStateTreeTasks.cpp
// StateTree Tasks and Conditions implementation for BossCharacter.

#include "BossStateTreeTasks.h"
#include "BossCharacter.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
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

//////////////////////////////////////////////////////////////////
// TASK: Boss Choose Action
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossChooseActionTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.Boss->ChooseNextAction();
	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FStateTreeBossChooseActionTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss: Choose Action (weighted Shoot/Melee)"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Boss Shoot
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossShootTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss || !InstanceData.Target || InstanceData.Boss->IsDisarmed())
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime = 0.0f;
	InstanceData.Boss->StartShootBurst(InstanceData.Target);

	// If the fire montage didn't start (none assigned), don't hang — finish immediately.
	if (!InstanceData.Boss->IsShootMontagePlaying())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeBossShootTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime += DeltaTime;

	// Fire montage ended (notify-driven burst complete) or safety cap reached.
	if (!InstanceData.Boss->IsShootMontagePlaying() || InstanceData.ElapsedTime >= InstanceData.MaxShootDuration)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeBossShootTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	if (InstanceData.Boss)
	{
		// Stop the burst if the state was left before it finished.
		InstanceData.Boss->StopShootBurst();
	}
}

#if WITH_EDITOR
FText FStateTreeBossShootTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss: Shoot (ranged burst)"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Boss Strafe
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossStrafeTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime = 0.0f;
	InstanceData.RepathTimer = 0.0f;
	InstanceData.Direction = FMath::RandBool() ? 1.0f : -1.0f;
	InstanceData.Duration = InstanceData.Boss->GetStrafeDurationForState();

	InstanceData.Boss->BeginStrafe(InstanceData.Target);
	InstanceData.Boss->StrafeStep(InstanceData.Target, InstanceData.Direction);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeBossStrafeTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		if (InstanceData.Boss)
		{
			InstanceData.Boss->StopStrafe();
		}
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime += DeltaTime;
	if (InstanceData.ElapsedTime >= InstanceData.Duration)
	{
		InstanceData.Boss->StopStrafe();
		return EStateTreeRunStatus::Succeeded;
	}

	InstanceData.RepathTimer += DeltaTime;
	if (InstanceData.RepathTimer >= InstanceData.Boss->GetStrafeRepathInterval())
	{
		InstanceData.RepathTimer = 0.0f;
		InstanceData.Boss->StrafeStep(InstanceData.Target, InstanceData.Direction);
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeBossStrafeTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	if (InstanceData.Boss)
	{
		InstanceData.Boss->StopStrafe();
	}
}

#if WITH_EDITOR
FText FStateTreeBossStrafeTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss: Strafe (orbit player)"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Is Disarmed
//////////////////////////////////////////////////////////////////

bool FStateTreeBossIsDisarmedCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	return InstanceData.Boss && InstanceData.Boss->IsDisarmed();
}

#if WITH_EDITOR
FText FStateTreeBossIsDisarmedCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Is Disarmed"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Pending Action Is
//////////////////////////////////////////////////////////////////

bool FStateTreeBossPendingActionIsCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	return InstanceData.Boss && InstanceData.Boss->GetPendingAction() == InstanceData.ExpectedAction;
}

#if WITH_EDITOR
FText FStateTreeBossPendingActionIsCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	if (Data)
	{
		const FString ActionName = (Data->ExpectedAction == EBossAction::Melee) ? TEXT("Melee") : TEXT("Shoot");
		return FText::FromString(FString::Printf(TEXT("Boss Pending Action is %s"), *ActionName));
	}
	return FText::FromString(TEXT("Boss Pending Action Is"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Boss Approach Target
//////////////////////////////////////////////////////////////////

namespace
{
	// Run up to the player, stopping at the boss's AttackRange (the lunge launch distance).
	void IssueBossApproachMove(ABossCharacter* Boss, AActor* Target)
	{
		if (AAIController* AI = Cast<AAIController>(Boss->GetController()))
		{
			// Acceptance radius must be comfortably INSIDE AttackRange. The approach task's
			// per-frame IsTargetInAttackRange (Dist <= AttackRange) is the real "start the attack"
			// trigger, and it has to fire while the boss is still MOVING. If the acceptance radius
			// equals AttackRange the boss stops exactly on the boundary, where the 3D distance check
			// flickers (capsule Z offset + path overshoot) and the attack won't start until the
			// player steps closer. Stopping short lets the boss cross the AttackRange line under
			// power, so the Tick reliably catches it and launches the lunge at ~AttackRange.
			const float ApproachAcceptance = Boss->GetAttackRange() * 0.5f;
			AI->MoveToActor(Target, ApproachAcceptance,
				/*bStopOnOverlap*/ false, /*bUsePathfinding*/ true, /*bCanStrafe*/ true);
		}
	}

	void StopBossMovement(ABossCharacter* Boss)
	{
		if (AAIController* AI = Cast<AAIController>(Boss->GetController()))
		{
			AI->StopMovement();
		}
	}
}

EStateTreeRunStatus FStateTreeBossApproachTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Already within lunge range → no approach needed, let the attack fire immediately.
	if (InstanceData.Boss->IsTargetInAttackRange(InstanceData.Target))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	InstanceData.Boss->SetTarget(InstanceData.Target); // focus → faces player while running up
	InstanceData.Boss->SetApproachSpeedActive(true);   // charge in faster than the strafe speed
	InstanceData.RepathTimer = 0.0f;
	IssueBossApproachMove(InstanceData.Boss, InstanceData.Target);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeBossApproachTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		if (InstanceData.Boss)
		{
			StopBossMovement(InstanceData.Boss);
		}
		return EStateTreeRunStatus::Failed;
	}

	// Reached lunge range → done; the melee attack launches the lunge from here.
	if (InstanceData.Boss->IsTargetInAttackRange(InstanceData.Target))
	{
		StopBossMovement(InstanceData.Boss);
		return EStateTreeRunStatus::Succeeded;
	}

	// Re-issue the move periodically so a moving/dodging player is still chased.
	InstanceData.RepathTimer += DeltaTime;
	if (InstanceData.RepathTimer >= InstanceData.RepathInterval)
	{
		InstanceData.RepathTimer = 0.0f;
		IssueBossApproachMove(InstanceData.Boss, InstanceData.Target);
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeBossApproachTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	if (InstanceData.Boss)
	{
		StopBossMovement(InstanceData.Boss);
		InstanceData.Boss->SetApproachSpeedActive(false); // restore the normal/strafe walk speed
	}
}

#if WITH_EDITOR
FText FStateTreeBossApproachTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss: Approach Target (run up to lunge range)"));
}
#endif
