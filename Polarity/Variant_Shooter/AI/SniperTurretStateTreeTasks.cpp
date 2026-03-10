// SniperTurretStateTreeTasks.cpp

#include "SniperTurretStateTreeTasks.h"
#include "SniperTurretNPC.h"
#include "StateTreeExecutionContext.h"

//////////////////////////////////////////////////////////////////
// TASK: Turret Aim at Target
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeTurretAimTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Turret)
	{
		UE_LOG(LogTemp, Warning, TEXT("TurretAimTask: Invalid Turret"));
		return EStateTreeRunStatus::Failed;
	}

	if (Data.Turret->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!Data.Target || !IsValid(Data.Target))
	{
		UE_LOG(LogTemp, Verbose, TEXT("TurretAimTask: No valid target"));
		return EStateTreeRunStatus::Failed;
	}

	// Check initial LOS and start aiming
	const bool bInitialLOS = Data.Turret->HasLineOfSightTo(Data.Target);
	Data.Turret->StartAiming(Data.Target);
	Data.Turret->SetLOSStatus(bInitialLOS);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeTurretAimTask::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Turret || Data.Turret->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!Data.Target || !IsValid(Data.Target))
	{
		Data.Turret->StopAiming();
		return EStateTreeRunStatus::Failed;
	}

	// Update LOS status each tick - turret class handles the rest
	const bool bLOS = Data.Turret->HasLineOfSightTo(Data.Target);
	Data.Turret->SetLOSStatus(bLOS);

	return EStateTreeRunStatus::Running;
}

void FStateTreeTurretAimTask::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Turret)
	{
		Data.Turret->StopAiming();
	}
}

#if WITH_EDITOR
FText FStateTreeTurretAimTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Aim turret at target (progressive lock-on with auto-fire)"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Is Turret Recovering
//////////////////////////////////////////////////////////////////

bool FStateTreeIsTurretRecoveringCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Turret)
	{
		return false;
	}

	return Data.Turret->IsInDamageRecovery();
}

#if WITH_EDITOR
FText FStateTreeIsTurretRecoveringCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Turret is recovering from damage interruption"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Is Turret Aiming
//////////////////////////////////////////////////////////////////

bool FStateTreeIsTurretAimingCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Turret)
	{
		return false;
	}

	return Data.Turret->IsAiming();
}

#if WITH_EDITOR
FText FStateTreeIsTurretAimingCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Turret is actively aiming at target"));
}
#endif
