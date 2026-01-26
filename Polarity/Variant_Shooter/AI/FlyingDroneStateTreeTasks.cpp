// FlyingDroneStateTreeTasks.cpp
// Implementation of StateTree Tasks and Conditions for FlyingDrone (flight and evasion)

#include "FlyingDroneStateTreeTasks.h"
#include "FlyingDrone.h"
#include "FlyingAIMovementComponent.h"
#include "StateTreeExecutionContext.h"

//////////////////////////////////////////////////////////////////
// TASK: Drone Evasive Dash
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeDroneEvasiveDashTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone)
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneEvasiveDashTask: Invalid Drone"));
		return EStateTreeRunStatus::Failed;
	}

	if (Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Check if can dash
	if (!Data.Drone->CanPerformEvasiveDash())
	{
		UE_LOG(LogTemp, Verbose, TEXT("DroneEvasiveDashTask: Dash on cooldown"));
		return EStateTreeRunStatus::Failed;
	}

	// Perform the dash
	if (!Data.Drone->PerformRandomEvasiveDash())
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneEvasiveDashTask: Failed to start dash"));
		return EStateTreeRunStatus::Failed;
	}

	// Clear damage flag since we're responding to it
	Data.Drone->ClearDamageTakenFlag();

	UE_LOG(LogTemp, Verbose, TEXT("DroneEvasiveDashTask: Started evasive dash"));
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeDroneEvasiveDashTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Check if dash is complete
	if (!Data.Drone->IsDashing())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeDroneEvasiveDashTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Nothing special needed
}

#if WITH_EDITOR
FText FStateTreeDroneEvasiveDashTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Perform evasive dash in random direction"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Drone Fly To Random Point
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeDroneFlyToRandomPointTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone)
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneFlyToRandomPointTask: Invalid Drone"));
		return EStateTreeRunStatus::Failed;
	}

	if (Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	UFlyingAIMovementComponent* FlyingMovement = Data.Drone->GetFlyingMovement();
	if (!FlyingMovement)
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneFlyToRandomPointTask: No FlyingMovement component"));
		return EStateTreeRunStatus::Failed;
	}

	FVector TargetPoint;
	bool bFoundPoint = false;

	// If we have a target to orbit around, generate point relative to them
	if (Data.TargetToOrbit)
	{
		const FVector TargetLocation = Data.TargetToOrbit->GetActorLocation();

		// Generate random point at a distance from target
		const float RandomAngle = FMath::RandRange(0.0f, 2.0f * PI);
		const float RandomDistance = FMath::RandRange(Data.MinDistanceFromTarget, Data.MaxDistanceFromTarget);

		FVector CandidatePoint = TargetLocation;
		CandidatePoint.X += FMath::Cos(RandomAngle) * RandomDistance;
		CandidatePoint.Y += FMath::Sin(RandomAngle) * RandomDistance;
		CandidatePoint.Z = Data.Drone->GetActorLocation().Z; // Keep current height initially

		// Validate through FlyingMovement (handles NavMesh projection and height)
		FVector ProjectedPoint;
		if (FlyingMovement->ProjectToNavMesh(CandidatePoint, ProjectedPoint))
		{
			TargetPoint = ProjectedPoint;
			bFoundPoint = true;
		}
	}

	// Fallback to patrol point generation
	if (!bFoundPoint)
	{
		bFoundPoint = FlyingMovement->GetRandomPatrolPoint(TargetPoint);
	}

	if (!bFoundPoint)
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneFlyToRandomPointTask: Failed to find valid point"));
		return EStateTreeRunStatus::Failed;
	}

	// Start flying to the point
	FlyingMovement->FlyToLocation(TargetPoint);

	UE_LOG(LogTemp, Verbose, TEXT("DroneFlyToRandomPointTask: Flying to (%.0f, %.0f, %.0f)"),
		TargetPoint.X, TargetPoint.Y, TargetPoint.Z);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeDroneFlyToRandomPointTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Check if movement is complete
	if (!Data.Drone->IsFlying())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeDroneFlyToRandomPointTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// Stop movement if we're exiting early
	if (Data.Drone && Transition.CurrentRunStatus != EStateTreeRunStatus::Succeeded)
	{
		Data.Drone->StopMovement();
	}
}

#if WITH_EDITOR
FText FStateTreeDroneFlyToRandomPointTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Fly to random point (NavMesh validated)"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Took Damage Recently
//////////////////////////////////////////////////////////////////

bool FStateTreeDroneTookDamageCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone)
	{
		return false;
	}

	return Data.Drone->TookDamageRecently(Data.GracePeriod);
}

#if WITH_EDITOR
FText FStateTreeDroneTookDamageCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Drone took damage recently"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Can Evasive Dash
//////////////////////////////////////////////////////////////////

bool FStateTreeDroneCanEvasiveDashCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone)
	{
		return false;
	}

	return Data.Drone->CanPerformEvasiveDash();
}

#if WITH_EDITOR
FText FStateTreeDroneCanEvasiveDashCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Drone can perform evasive dash (off cooldown)"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Is Flying
//////////////////////////////////////////////////////////////////

bool FStateTreeDroneIsFlyingCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone)
	{
		return false;
	}

	return Data.Drone->IsFlying();
}

#if WITH_EDITOR
FText FStateTreeDroneIsFlyingCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Drone is currently flying to destination"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Is Dashing
//////////////////////////////////////////////////////////////////

bool FStateTreeDroneIsDashingCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone)
	{
		return false;
	}

	return Data.Drone->IsDashing();
}

#if WITH_EDITOR
FText FStateTreeDroneIsDashingCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Drone is currently dashing"));
}
#endif
