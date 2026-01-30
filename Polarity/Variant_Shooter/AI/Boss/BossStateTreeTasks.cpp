// BossStateTreeTasks.cpp
// StateTree Tasks and Conditions implementation for BossCharacter

#include "BossStateTreeTasks.h"
#include "BossCharacter.h"
#include "StateTreeExecutionContext.h"

//////////////////////////////////////////////////////////////////
// TASK: Boss Approach Dash
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossApproachDashTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossApproachDash] FAILED - Missing Boss or Target"));
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.Boss->CanDash())
	{
		return EStateTreeRunStatus::Failed;
	}

	bool bStarted = InstanceData.Boss->StartApproachDash(InstanceData.Target);
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

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossCircleDash] FAILED - Missing Boss or Target"));
		return EStateTreeRunStatus::Failed;
	}

	// Note: Circle Dash does NOT check CanDash() cooldown
	// This allows it to chain immediately after Approach Dash
	// The cooldown is checked before the next Approach Dash instead

	bool bStarted = InstanceData.Boss->StartCircleDash(InstanceData.Target);
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

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.Boss->CanMeleeAttack())
	{
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

	// Wait for attack to complete
	if (InstanceData.Boss->IsAttacking())
	{
		return EStateTreeRunStatus::Running;
	}

	return EStateTreeRunStatus::Succeeded;
}

void FStateTreeBossMeleeAttackTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Attack cleanup handled by boss
}

#if WITH_EDITOR
FText FStateTreeBossMeleeAttackTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss: Melee Attack"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Boss Start Hovering
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossStartHoveringTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return EStateTreeRunStatus::Failed;
	}

	// StartHovering is called by SetPhase/ExecutePhaseTransition when phase changes to Aerial
	// If we're already transitioning, just wait for it
	// If not transitioning and already in Aerial phase, we're done
	if (!InstanceData.Boss->IsTransitioning())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	UE_LOG(LogTemp, Warning, TEXT("[BossStartHovering] Waiting for takeoff to complete..."));
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeBossStartHoveringTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Wait for transition to complete
	if (InstanceData.Boss->IsTransitioning())
	{
		return EStateTreeRunStatus::Running;
	}

	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FStateTreeBossStartHoveringTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss: Start Hovering (Wait for takeoff)"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Boss Stop Hovering
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossStopHoveringTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return EStateTreeRunStatus::Failed;
	}

	// StopHovering is called by SetPhase/ExecutePhaseTransition when phase changes to Ground
	// If we're already transitioning, just wait for it
	// If not transitioning and already in Ground phase, we're done
	if (!InstanceData.Boss->IsTransitioning())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	UE_LOG(LogTemp, Warning, TEXT("[BossStopHovering] Waiting for landing to complete..."));
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeBossStopHoveringTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Wait for transition to complete
	if (InstanceData.Boss->IsTransitioning())
	{
		return EStateTreeRunStatus::Running;
	}

	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FStateTreeBossStopHoveringTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss: Stop Hovering (Wait for landing)"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Boss Aerial Strafe
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossAerialStrafeTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Reset timer and pick random strafe direction
	InstanceData.ElapsedTime = 0.0f;
	InstanceData.StrafeDirection = FMath::VRand();
	InstanceData.StrafeDirection.Z = 0.0f;
	InstanceData.StrafeDirection.Normalize();

	// 50% chance to strafe left or right
	if (FMath::RandBool())
	{
		InstanceData.StrafeDirection = -InstanceData.StrafeDirection;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeBossAerialStrafeTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime += DeltaTime;

	if (InstanceData.ElapsedTime >= InstanceData.StrafeDuration)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// Perform strafe
	InstanceData.Boss->AerialStrafe(InstanceData.StrafeDirection);

	return EStateTreeRunStatus::Running;
}

#if WITH_EDITOR
FText FStateTreeBossAerialStrafeTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss: Aerial Strafe"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Boss Aerial Dash
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossAerialDashTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return EStateTreeRunStatus::Failed;
	}

	bool bStarted = InstanceData.Boss->PerformAerialDash();
	return bStarted ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FStateTreeBossAerialDashTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Check if flying movement dash is done
	// For now, immediately succeed - aerial dash is quick
	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FStateTreeBossAerialDashTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss: Aerial Dash"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Boss Match Opposite Polarity
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossMatchPolarityTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.Boss->MatchOppositePolarity(InstanceData.Target);
	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FStateTreeBossMatchPolarityTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss: Match Opposite Polarity"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Boss Shoot
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeBossShootTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Use BossCharacter's custom projectile firing (spawns BossProjectile with parry detection)
	InstanceData.Boss->FireEMFProjectile(InstanceData.Target);
	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FStateTreeBossShootTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss: Shoot at Target"));
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
		FString PhaseName;
		switch (Data->NewPhase)
		{
		case EBossPhase::Ground: PhaseName = TEXT("Ground"); break;
		case EBossPhase::Aerial: PhaseName = TEXT("Aerial"); break;
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

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Phase Is
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
		case EBossPhase::Aerial: PhaseName = TEXT("Aerial"); break;
		case EBossPhase::Finisher: PhaseName = TEXT("Finisher"); break;
		default: PhaseName = TEXT("Unknown"); break;
		}
		return FText::FromString(FString::Printf(TEXT("Boss Phase is %s"), *PhaseName));
	}
	return FText::FromString(TEXT("Boss Phase is"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Should Transition To Aerial
//////////////////////////////////////////////////////////////////

bool FStateTreeBossShouldGoAerialCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return false;
	}

	return InstanceData.Boss->ShouldTransitionToAerial();
}

#if WITH_EDITOR
FText FStateTreeBossShouldGoAerialCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Should Transition To Aerial"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Should Transition To Ground
//////////////////////////////////////////////////////////////////

bool FStateTreeBossShouldGoGroundCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return false;
	}

	return InstanceData.Boss->ShouldTransitionToGround();
}

#if WITH_EDITOR
FText FStateTreeBossShouldGoGroundCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Should Transition To Ground"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Can Dash
//////////////////////////////////////////////////////////////////

bool FStateTreeBossCanDashCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return false;
	}

	return InstanceData.Boss->CanDash();
}

#if WITH_EDITOR
FText FStateTreeBossCanDashCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Can Dash"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Can Melee Attack
//////////////////////////////////////////////////////////////////

bool FStateTreeBossCanMeleeCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return false;
	}

	return InstanceData.Boss->CanMeleeAttack();
}

#if WITH_EDITOR
FText FStateTreeBossCanMeleeCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Can Melee Attack"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Is Dashing
//////////////////////////////////////////////////////////////////

bool FStateTreeBossIsDashingCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return false;
	}

	return InstanceData.Boss->IsDashing();
}

#if WITH_EDITOR
FText FStateTreeBossIsDashingCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Is Dashing"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Is In Melee Range
//////////////////////////////////////////////////////////////////

bool FStateTreeBossInMeleeRangeCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		return false;
	}

	return InstanceData.Boss->IsTargetInMeleeRange(InstanceData.Target);
}

#if WITH_EDITOR
FText FStateTreeBossInMeleeRangeCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Is In Melee Range"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Is In Finisher Phase
//////////////////////////////////////////////////////////////////

bool FStateTreeBossInFinisherCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss)
	{
		return false;
	}

	return InstanceData.Boss->IsInFinisherPhase();
}

#if WITH_EDITOR
FText FStateTreeBossInFinisherCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Is In Finisher Phase"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Target Is Far
//////////////////////////////////////////////////////////////////

bool FStateTreeBossTargetIsFarCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		return false;
	}

	return InstanceData.Boss->IsTargetFar(InstanceData.Target);
}

#if WITH_EDITOR
FText FStateTreeBossTargetIsFarCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Target Is Far"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Boss Target Is Close
//////////////////////////////////////////////////////////////////

bool FStateTreeBossTargetIsCloseCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);

	if (!InstanceData.Boss || !InstanceData.Target)
	{
		return false;
	}

	// Opposite of IsTargetFar
	return !InstanceData.Boss->IsTargetFar(InstanceData.Target);
}

#if WITH_EDITOR
FText FStateTreeBossTargetIsCloseCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Boss Target Is Close"));
}
#endif
