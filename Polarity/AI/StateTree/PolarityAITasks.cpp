// PolarityAITasks.cpp

#include "PolarityAITasks.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "../Coordination/AICombatCoordinator.h"
#include "../Components/MeleeRetreatComponent.h"
#include "../../Variant_Shooter/AI/ShooterNPC.h"
#include "../../Variant_Shooter/AI/FlyingDrone.h"
#include "../../Variant_Shooter/AI/FlyingAIMovementComponent.h"
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

// ============================================================================
// BurstFire
// ============================================================================

EStateTreeRunStatus FSTTask_BurstFire::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC || !Data.Target)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (Data.NPC->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	Data.bStartedShooting = false;

	// Request permission from coordinator if needed
	if (Data.bUseCoordinator)
	{
		AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
		if (Coordinator && !Coordinator->RequestAttackPermission(Data.NPC))
		{
			// No permission - fail (let StateTree handle retry)
			return EStateTreeRunStatus::Failed;
		}
	}

	// Start shooting (with external permission flag since we already got it)
	Data.NPC->StartShooting(Data.Target, true);
	Data.bStartedShooting = true;

	// Notify coordinator
	if (Data.bUseCoordinator)
	{
		if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC))
		{
			Coordinator->NotifyAttackStarted(Data.NPC);
		}
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_BurstFire::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC || Data.NPC->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Check if burst completed (NPC entered burst cooldown)
	if (Data.bStartedShooting && Data.NPC->IsInBurstCooldown())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// Also check if shooting stopped for any reason
	if (Data.bStartedShooting && !Data.NPC->IsCurrentlyShooting())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_BurstFire::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.NPC && Data.bStartedShooting)
	{
		Data.NPC->StopShooting();

		// Release coordinator permission
		if (Data.bUseCoordinator)
		{
			if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC))
			{
				Coordinator->NotifyAttackComplete(Data.NPC);
			}
		}
	}
}

#if WITH_EDITOR
FText FSTTask_BurstFire::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return NSLOCTEXT("PolarityAI", "BurstFireDesc", "Fire burst at target (uses NPC burst settings)");
}
#endif

// ============================================================================
// FlyAndShoot
// ============================================================================

EStateTreeRunStatus FSTTask_FlyAndShoot::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || !Data.Target)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Reset state
	Data.bHasDestination = false;
	Data.bIsShooting = false;
	Data.CurrentDestination = FVector::ZeroVector;

	// Pick first destination
	if (!PickNewDestination(Data))
	{
		UE_LOG(LogTemp, Warning, TEXT("FlyAndShoot: Failed to pick initial destination"));
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_FlyAndShoot::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!Data.Target)
	{
		return EStateTreeRunStatus::Failed;
	}

	UFlyingAIMovementComponent* FlyingMovement = Data.Drone->GetFlyingMovement();
	if (!FlyingMovement)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Check if we reached destination and pick new one
	if (Data.bHasDestination)
	{
		const FVector DroneLocation = Data.Drone->GetActorLocation();
		const float DistanceToDestination = FVector::Dist(DroneLocation, Data.CurrentDestination);

		if (DistanceToDestination <= Data.AcceptanceRadius || !FlyingMovement->IsMoving())
		{
			// Reached destination or movement stopped - pick new one
			PickNewDestination(Data);
		}
	}

	// Handle shooting - check if we can shoot
	if (!Data.bIsShooting)
	{
		// Not currently shooting - check if we can start
		if (CanShoot(Data))
		{
			StartShooting(Data);
		}
	}
	else
	{
		// Currently shooting - check if burst completed
		if (Data.Drone->IsInBurstCooldown())
		{
			// Burst finished, entering cooldown - just update our state flag
			// Don't call StopShooting() as that sets bWantsToShoot=false and prevents auto-resume
			Data.bIsShooting = false;

			// Release coordinator permission during cooldown
			if (Data.bUseCoordinator)
			{
				if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.Drone))
				{
					Coordinator->NotifyAttackComplete(Data.Drone);
				}
			}
		}
		else if (!Data.Drone->IsCurrentlyShooting())
		{
			// Stopped shooting for other reason (interrupted, etc.)
			StopShooting(Data);
		}
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_FlyAndShoot::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Drone)
	{
		// Stop shooting
		if (Data.bIsShooting)
		{
			StopShooting(Data);
		}

		// Stop movement
		if (UFlyingAIMovementComponent* FlyingMovement = Data.Drone->GetFlyingMovement())
		{
			FlyingMovement->StopMovement();
		}
	}
}

bool FSTTask_FlyAndShoot::PickNewDestination(FInstanceDataType& Data) const
{
	if (!Data.Drone || !Data.Target)
	{
		return false;
	}

	UFlyingAIMovementComponent* FlyingMovement = Data.Drone->GetFlyingMovement();
	if (!FlyingMovement)
	{
		return false;
	}

	// Get random point around target
	const FVector TargetLocation = Data.Target->GetActorLocation();
	FVector NewPoint;

	if (FlyingMovement->GetRandomPointInVolume(TargetLocation, Data.OrbitRadius, Data.MinHeight, Data.MaxHeight, NewPoint))
	{
		Data.CurrentDestination = NewPoint;
		Data.bHasDestination = true;

		// Start flying to new destination
		FlyingMovement->FlyToLocation(NewPoint, Data.AcceptanceRadius);

		return true;
	}

	return false;
}

bool FSTTask_FlyAndShoot::CanShoot(const FInstanceDataType& Data) const
{
	if (!Data.Drone || !Data.Target)
	{
		return false;
	}

	// Don't shoot if dead
	if (Data.Drone->IsDead())
	{
		return false;
	}

	// Don't shoot if in burst cooldown
	if (Data.Drone->IsInBurstCooldown())
	{
		return false;
	}

	// Don't shoot if already shooting
	if (Data.Drone->IsCurrentlyShooting())
	{
		return false;
	}

	// Check line of sight
	if (!Data.Drone->HasLineOfSightTo(Data.Target))
	{
#if WITH_EDITOR
		UE_LOG(LogTemp, Warning, TEXT("FlyAndShoot: No LOS to target"));
#endif
		return false;
	}

	// Check coordinator permission if needed
	if (Data.bUseCoordinator)
	{
		AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.Drone);
		if (Coordinator && !Coordinator->RequestAttackPermission(Data.Drone))
		{
#if WITH_EDITOR
			UE_LOG(LogTemp, Warning, TEXT("FlyAndShoot: Coordinator denied permission"));
#endif
			return false;
		}
	}

	return true;
}

void FSTTask_FlyAndShoot::StartShooting(FInstanceDataType& Data) const
{
	if (!Data.Drone || !Data.Target)
	{
		return;
	}

	// Start shooting (with external permission since we already checked coordinator)
	Data.Drone->StartShooting(Data.Target, true);
	Data.bIsShooting = true;

	// Notify coordinator that attack started
	if (Data.bUseCoordinator)
	{
		if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.Drone))
		{
			Coordinator->NotifyAttackStarted(Data.Drone);
		}
	}
}

void FSTTask_FlyAndShoot::StopShooting(FInstanceDataType& Data) const
{
	if (!Data.Drone)
	{
		return;
	}

	Data.Drone->StopShooting();
	Data.bIsShooting = false;

	// Notify coordinator that attack completed
	if (Data.bUseCoordinator)
	{
		if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.Drone))
		{
			Coordinator->NotifyAttackComplete(Data.Drone);
		}
	}
}

#if WITH_EDITOR
FText FSTTask_FlyAndShoot::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	if (Data)
	{
		return FText::Format(NSLOCTEXT("PolarityAI", "FlyAndShootDesc",
			"Fly around target (radius: {0}) and shoot when ready"), FText::AsNumber(static_cast<int32>(Data->OrbitRadius)));
	}
	return NSLOCTEXT("PolarityAI", "FlyAndShootDescDefault", "Fly around target and shoot when ready");
}
#endif