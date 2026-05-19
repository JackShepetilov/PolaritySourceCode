// BossStateTreeTasks.cpp
// StateTree Tasks and Conditions implementation for BossCharacter

#include "BossStateTreeTasks.h"
#include "BossCharacter.h"
#include "StateTreeExecutionContext.h"
#include "GameFramework/CharacterMovementComponent.h"

//////////////////////////////////////////////////////////////////
// TASK: Boss Approach Dash
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossApproachDashTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	const bool bBossOK = InstanceData.Boss != nullptr;
	const bool bTargetOK = InstanceData.Target != nullptr;
	const bool bCanDash = bBossOK && InstanceData.Boss->CanDash();
	UE_LOG(LogTemp, Warning, TEXT("[BOSS_TASK] ApproachDash::EnterState - Boss=%d Target=%d CanDash=%d"),
		bBossOK, bTargetOK, bCanDash);

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossApproachDash] FAILED - Missing Boss or Target"));
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.Boss->CanDash())
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossApproachDash] FAILED - CanDash returned false"));
		return EStateTreeRunStatus::Failed;
	}

	bool bStarted = InstanceData.Boss->StartApproachDash(InstanceData.Target);
	UE_LOG(LogTemp, Warning, TEXT("[BossApproachDash] StartApproachDash returned %d"), bStarted);
	return bStarted ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FStateTreeBossApproachDashTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.Boss->IsDashing())
	{
		return EStateTreeRunStatus::Running;
	}

	return EStateTreeRunStatus::Succeeded;
}

void FStateTreeBossApproachDashTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
}

#if WITH_EDITOR
FText FStateTreeBossApproachDashTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss: Approach Dash (to player)"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Boss Circle Dash
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossCircleDashTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	UE_LOG(LogTemp, Warning, TEXT("[BOSS_TASK] CircleDash::EnterState - Boss=%d Target=%d"),
		InstanceData.Boss != nullptr, InstanceData.Target != nullptr);

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossCircleDash] FAILED - Missing Boss or Target"));
		return EStateTreeRunStatus::Failed;
	}

	// Note: Circle Dash does NOT check CanDash() cooldown
	// This allows it to chain immediately after Approach Dash
	// The cooldown is checked before the next Approach Dash instead

	bool bStarted = InstanceData.Boss->StartCircleDash(InstanceData.Target);
	UE_LOG(LogTemp, Warning, TEXT("[BossCircleDash] StartCircleDash returned %d"), bStarted);
	return bStarted ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FStateTreeBossCircleDashTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.Boss->IsDashing())
	{
		return EStateTreeRunStatus::Running;
	}

	return EStateTreeRunStatus::Succeeded;
}

void FStateTreeBossCircleDashTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
}

#if WITH_EDITOR
FText FStateTreeBossCircleDashTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss: Circle Dash (around player)"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Boss Melee Attack
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossMeleeAttackTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	const bool bBossOK = InstanceData.Boss != nullptr;
	const bool bTargetOK = InstanceData.Target != nullptr;
	const bool bCanMelee = bBossOK && InstanceData.Boss->CanMeleeAttack();
	UE_LOG(LogTemp, Warning, TEXT("[BOSS_TASK] MeleeAttack::EnterState - Boss=%d Target=%d CanMelee=%d"),
		bBossOK, bTargetOK, bCanMelee);

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossMeleeAttack] FAILED - Missing Boss or Target"));
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.Boss->CanMeleeAttack())
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossMeleeAttack] FAILED - CanMeleeAttack returned false"));
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.Boss->StartMeleeAttack(InstanceData.Target);
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
	return FText::FromString(TEXT("Boss: Melee Attack"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Boss Enter Finisher Phase
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossEnterFinisherTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	UE_LOG(LogTemp, Warning, TEXT("[BOSS_TASK] EnterFinisher::EnterState - Boss=%d"),
		InstanceData.Boss != nullptr);

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
		UE_LOG(LogTemp, Error, TEXT("[BossSetPhase] FAILED - Boss is NULL"));
		return EStateTreeRunStatus::Failed;
	}

	// Rate-limited diagnostic so StateTree spinning on this task doesn't flood the log.
	static double LastSetPhaseTaskLogTime = -10.0;
	const double NowSec = InstanceData.Boss->GetWorld() ? InstanceData.Boss->GetWorld()->GetTimeSeconds() : 0.0;
	if (NowSec - LastSetPhaseTaskLogTime >= 5.0)
	{
		LastSetPhaseTaskLogTime = NowSec;

		FString PhaseNames[] = { TEXT("Ground"), TEXT("Finisher") };
		const int32 PhaseIdx = (int)InstanceData.NewPhase;
		UE_LOG(LogTemp, Warning, TEXT("[BOSS_TASK] SetPhase::EnterState - NewPhase(raw)=%d, named=%s"),
			PhaseIdx, PhaseIdx >= 0 && PhaseIdx < 2 ? *PhaseNames[PhaseIdx] : TEXT("Unknown"));
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
		FString PhaseName;
		switch (Data->NewPhase)
		{
		case EBossPhase::Ground: PhaseName = TEXT("Ground"); break;
		case EBossPhase::Finisher: PhaseName = TEXT("Finisher"); break;
		default: PhaseName = TEXT("Unknown"); break;
		}
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

	if (!InstanceData.Boss)
	{
		return false;
	}

	return InstanceData.Boss->GetCurrentPhase() == InstanceData.ExpectedPhase;
}

#if WITH_EDITOR
FText FStateTreeBossPhaseIsCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	if (Data)
	{
		FString PhaseName;
		switch (Data->ExpectedPhase)
		{
		case EBossPhase::Ground: PhaseName = TEXT("Ground"); break;
		case EBossPhase::Finisher: PhaseName = TEXT("Finisher"); break;
		default: PhaseName = TEXT("Unknown"); break;
		}
		return FText::FromString(FString::Printf(TEXT("Boss Phase is %s"), *PhaseName));
	}
	return FText::FromString(TEXT("Boss Phase is"));
}
#endif

bool FStateTreeBossCanDashCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	return InstanceData.Boss && InstanceData.Boss->CanDash();
}

#if WITH_EDITOR
FText FStateTreeBossCanDashCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Can Dash"));
}
#endif

bool FStateTreeBossCanMeleeCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	return InstanceData.Boss && InstanceData.Boss->CanMeleeAttack();
}

#if WITH_EDITOR
FText FStateTreeBossCanMeleeCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Can Melee Attack"));
}
#endif

bool FStateTreeBossIsDashingCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	return InstanceData.Boss && InstanceData.Boss->IsDashing();
}

#if WITH_EDITOR
FText FStateTreeBossIsDashingCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Is Dashing"));
}
#endif

bool FStateTreeBossInMeleeRangeCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	return InstanceData.Boss && InstanceData.Target && InstanceData.Boss->IsTargetInMeleeRange(InstanceData.Target);
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

bool FStateTreeBossTargetIsFarCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	return InstanceData.Boss && InstanceData.Target && InstanceData.Boss->IsTargetFar(InstanceData.Target);
}

#if WITH_EDITOR
FText FStateTreeBossTargetIsFarCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Target Is Far"));
}
#endif

bool FStateTreeBossTargetIsCloseCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	return InstanceData.Boss && InstanceData.Target && !InstanceData.Boss->IsTargetFar(InstanceData.Target);
}

#if WITH_EDITOR
FText FStateTreeBossTargetIsCloseCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Target Is Close"));
}
#endif

bool FStateTreeBossIsOnGroundCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	if (!InstanceData.Boss)
	{
		return false;
	}
	if (UCharacterMovementComponent* MovementComp = InstanceData.Boss->GetCharacterMovement())
	{
		return MovementComp->IsMovingOnGround();
	}
	return false;
}

#if WITH_EDITOR
FText FStateTreeBossIsOnGroundCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Is On Ground"));
}
#endif
