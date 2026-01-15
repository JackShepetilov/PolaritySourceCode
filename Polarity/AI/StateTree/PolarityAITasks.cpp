// PolarityAITasks.cpp

#include "PolarityAITasks.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "../Coordination/AICombatCoordinator.h"
#include "../Components/MeleeRetreatComponent.h"
#include "ShooterNPC.h"
#include "Navigation/PathFollowingComponent.h"
#include "AITypes.h"

// ============================================================================
// RequestAttackPermission
// ============================================================================

EStateTreeRunStatus FSTTask_RequestAttackPermission::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC)
	{
		Data.bPermissionGranted = false;
		return EStateTreeRunStatus::Failed;
	}

	AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
	if (!Coordinator)
	{
		// No coordinator = permission always granted
		Data.bPermissionGranted = true;
		return EStateTreeRunStatus::Succeeded;
	}

	Data.bPermissionGranted = Coordinator->RequestAttackPermission(Data.NPC);
	return Data.bPermissionGranted ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}

#if WITH_EDITOR
FText FSTTask_RequestAttackPermission::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return NSLOCTEXT("PolarityAI", "RequestAttackPermissionDesc", "Request attack permission from coordinator");
}
#endif

// ============================================================================
// NotifyAttackComplete
// ============================================================================

EStateTreeRunStatus FSTTask_NotifyAttackComplete::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC))
	{
		Coordinator->NotifyAttackComplete(Data.NPC);
	}

	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FSTTask_NotifyAttackComplete::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return NSLOCTEXT("PolarityAI", "NotifyAttackCompleteDesc", "Notify coordinator that attack is complete");
}
#endif

// ============================================================================
// ExecuteRetreat
// ============================================================================

EStateTreeRunStatus FSTTask_ExecuteRetreat::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC || !Data.Controller)
	{
		return EStateTreeRunStatus::Failed;
	}

	UMeleeRetreatComponent* RetreatComp = Data.NPC->FindComponentByClass<UMeleeRetreatComponent>();
	if (!RetreatComp || !RetreatComp->IsRetreating())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Move to retreat destination
	const FVector Destination = RetreatComp->GetRetreatDestination();
	Data.Controller->MoveToLocation(Destination, Data.AcceptanceRadius, true, true, false, true);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_ExecuteRetreat::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC)
	{
		return EStateTreeRunStatus::Failed;
	}

	UMeleeRetreatComponent* RetreatComp = Data.NPC->FindComponentByClass<UMeleeRetreatComponent>();
	if (!RetreatComp)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Check if retreat is complete
	if (!RetreatComp->IsRetreating())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// Check if reached destination
	if (Data.Controller)
	{
		if (UPathFollowingComponent* PathComp = Data.Controller->GetPathFollowingComponent())
		{
			if (PathComp->DidMoveReachGoal())
			{
				return EStateTreeRunStatus::Succeeded;
			}
		}
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ExecuteRetreat::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Controller)
	{
		Data.Controller->StopMovement();
	}
}

#if WITH_EDITOR
FText FSTTask_ExecuteRetreat::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return NSLOCTEXT("PolarityAI", "ExecuteRetreatDesc", "Execute retreat movement");
}
#endif

// ============================================================================
// ShootWithAccuracy
// ============================================================================

EStateTreeRunStatus FSTTask_ShootWithAccuracy::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC || !Data.Target)
	{
		return EStateTreeRunStatus::Failed;
	}

	Data.ElapsedTime = 0.0f;

	// Start shooting with external permission flag (StateTree already got permission)
	Data.NPC->StartShooting(Data.Target, true);

	// Notify coordinator that attack started
	if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC))
	{
		Coordinator->NotifyAttackStarted(Data.NPC);
	}

	// Instant shot or sustained fire?
	if (Data.ShootDuration <= 0.0f)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_ShootWithAccuracy::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	Data.ElapsedTime += DeltaTime;

	if (Data.ElapsedTime >= Data.ShootDuration)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ShootWithAccuracy::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.NPC)
	{
		Data.NPC->StopShooting();
	}
}

#if WITH_EDITOR
FText FSTTask_ShootWithAccuracy::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	if (Data && Data->ShootDuration > 0.0f)
	{
		return FText::Format(NSLOCTEXT("PolarityAI", "ShootWithAccuracyDurationDesc",
			"Shoot at target for {0}s"), FText::AsNumber(Data->ShootDuration));
	}
	return NSLOCTEXT("PolarityAI", "ShootWithAccuracyDesc", "Shoot at target (single shot)");
}
#endif

// ============================================================================
// RegisterWithCoordinator
// ============================================================================

EStateTreeRunStatus FSTTask_RegisterWithCoordinator::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC))
	{
		Coordinator->RegisterNPC(Data.NPC);
	}

	return EStateTreeRunStatus::Running; // Stay registered while state is active
}

void FSTTask_RegisterWithCoordinator::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.NPC)
	{
		if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC))
		{
			Coordinator->UnregisterNPC(Data.NPC);
		}
	}
}

#if WITH_EDITOR
FText FSTTask_RegisterWithCoordinator::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return NSLOCTEXT("PolarityAI", "RegisterWithCoordinatorDesc", "Register NPC with combat coordinator");
}
#endif

// ============================================================================
// TriggerRetreat
// ============================================================================

EStateTreeRunStatus FSTTask_TriggerRetreat::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC || !Data.Attacker)
	{
		Data.bRetreatTriggered = false;
		return EStateTreeRunStatus::Failed;
	}

	UMeleeRetreatComponent* RetreatComp = Data.NPC->FindComponentByClass<UMeleeRetreatComponent>();
	if (!RetreatComp)
	{
		Data.bRetreatTriggered = false;
		return EStateTreeRunStatus::Failed;
	}

	Data.bRetreatTriggered = RetreatComp->TriggerRetreat(Data.Attacker);
	return Data.bRetreatTriggered ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}

#if WITH_EDITOR
FText FSTTask_TriggerRetreat::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return NSLOCTEXT("PolarityAI", "TriggerRetreatDesc", "Trigger retreat from attacker");
}
#endif

// ============================================================================
// MoveWithStrafe - Move while keeping focus on target
// ============================================================================

EStateTreeRunStatus FSTTask_MoveWithStrafe::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Controller)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Set focus on target to enable strafing
	if (IsValid(Data.FocusTarget))
	{
		Data.Controller->SetFocus(Data.FocusTarget);
	}

	// Start movement with strafe enabled
	FAIMoveRequest MoveRequest;
	MoveRequest.SetGoalLocation(Data.Destination);
	MoveRequest.SetAcceptanceRadius(Data.AcceptanceRadius);
	MoveRequest.SetUsePathfinding(Data.bUsePathfinding);
	MoveRequest.SetAllowPartialPath(true);
	MoveRequest.SetProjectGoalLocation(true);
	MoveRequest.SetCanStrafe(true);  // Enable strafing!

	Data.Controller->MoveTo(MoveRequest);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_MoveWithStrafe::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Controller)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Update focus if target moved
	if (IsValid(Data.FocusTarget))
	{
		Data.Controller->SetFocus(Data.FocusTarget);
	}

	// Check if reached destination
	if (UPathFollowingComponent* PathComp = Data.Controller->GetPathFollowingComponent())
	{
		if (PathComp->DidMoveReachGoal())
		{
			return EStateTreeRunStatus::Succeeded;
		}

		// Check if movement failed
		if (PathComp->GetStatus() == EPathFollowingStatus::Idle)
		{
			return EStateTreeRunStatus::Failed;
		}
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_MoveWithStrafe::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Controller)
	{
		Data.Controller->StopMovement();
		Data.Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

#if WITH_EDITOR
FText FSTTask_MoveWithStrafe::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return NSLOCTEXT("PolarityAI", "MoveWithStrafeDesc", "Move to destination while strafing (looking at focus target)");
}
#endif