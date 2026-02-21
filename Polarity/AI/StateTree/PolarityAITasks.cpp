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
#include "NavigationSystem.h"
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
		UE_LOG(LogTemp, Warning, TEXT("MoveWithStrafe: No Controller!"));
		return EStateTreeRunStatus::Failed;
	}

	// Check if destination is valid (not zero vector)
	if (Data.Destination.IsNearlyZero())
	{
		UE_LOG(LogTemp, Warning, TEXT("MoveWithStrafe: Destination is zero!"));
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

	const FPathFollowingRequestResult Result = Data.Controller->MoveTo(MoveRequest);

	UE_LOG(LogTemp, Log, TEXT("MoveWithStrafe: MoveTo result=%d, Destination=%s"),
		static_cast<int32>(Result.Code), *Data.Destination.ToString());

	// Check immediate move result
	if (Result.Code == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogTemp, Warning, TEXT("MoveWithStrafe: MoveTo failed immediately!"));
		return EStateTreeRunStatus::Failed;
	}

	if (Result.Code == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		UE_LOG(LogTemp, Log, TEXT("MoveWithStrafe: Already at goal"));
		return EStateTreeRunStatus::Succeeded;
	}

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
		const EPathFollowingStatus::Type Status = PathComp->GetStatus();

		if (PathComp->DidMoveReachGoal())
		{
			UE_LOG(LogTemp, Log, TEXT("MoveWithStrafe: Reached goal"));
			return EStateTreeRunStatus::Succeeded;
		}

		// Only fail if we're idle AND we've been trying for a while
		// (Idle right after MoveTo can happen if path is being calculated)
		if (Status == EPathFollowingStatus::Idle)
		{
			// Check distance to destination - if we're close enough, consider it success
			if (APawn* Pawn = Data.Controller->GetPawn())
			{
				const float DistToGoal = FVector::Dist(Pawn->GetActorLocation(), Data.Destination);
				if (DistToGoal <= Data.AcceptanceRadius * 1.5f)
				{
					UE_LOG(LogTemp, Log, TEXT("MoveWithStrafe: Close enough to goal (dist=%.0f)"), DistToGoal);
					return EStateTreeRunStatus::Succeeded;
				}
			}

			UE_LOG(LogTemp, Warning, TEXT("MoveWithStrafe: PathFollowing is Idle - movement may have failed"));
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
	Data.LastLOSTime = Data.Drone->GetWorld()->GetTimeSeconds();

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

	// Track LOS status for repositioning
	const bool bHasLOS = Data.Drone->HasLineOfSightTo(Data.Target);
	const float CurrentTime = Data.Drone->GetWorld()->GetTimeSeconds();

	if (bHasLOS)
	{
		Data.LastLOSTime = CurrentTime;
	}

	// Check if we reached destination and pick new one
	if (Data.bHasDestination)
	{
		const FVector DroneLocation = Data.Drone->GetActorLocation();
		const float DistanceToDestination = FVector::Dist(DroneLocation, Data.CurrentDestination);

		bool bNeedsNewDestination = false;

		if (DistanceToDestination <= Data.AcceptanceRadius || !FlyingMovement->IsMoving())
		{
			// Reached destination or movement stopped
			bNeedsNewDestination = true;
		}
		else if (!bHasLOS && (CurrentTime - Data.LastLOSTime) > FlyAndShoot_LOSLostRepositionTime)
		{
			// No LOS for too long — interrupt current path to find a position with LOS
			FlyingMovement->StopMovement();
			bNeedsNewDestination = true;

			// Reset timer so the drone has time to reach the new destination
			// before we force another reposition
			Data.LastLOSTime = CurrentTime;
		}

		if (bNeedsNewDestination)
		{
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
		// Currently shooting - check if LOS was lost mid-burst
		if (!Data.Drone->HasLineOfSightTo(Data.Target))
		{
			// LOS lost - stop shooting immediately to avoid firing through walls
			StopShooting(Data);
		}
		else if (Data.Drone->IsInBurstCooldown())
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

	const FVector TargetLocation = Data.Target->GetActorLocation();
	const bool bCurrentlyHasLOS = Data.Drone->HasLineOfSightTo(Data.Target);

	// Try multiple points, prefer ones with LOS to target
	constexpr int32 MaxAttempts = 8;
	FVector FallbackPoint = FVector::ZeroVector;
	bool bHasFallback = false;

	for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	{
		FVector NewPoint;
		if (!FlyingMovement->GetRandomPointInVolume(TargetLocation, Data.OrbitRadius, Data.MinHeight, Data.MaxHeight, NewPoint))
		{
			continue;
		}

		// Save first valid point as fallback
		if (!bHasFallback)
		{
			FallbackPoint = NewPoint;
			bHasFallback = true;
		}

		// Check LOS from candidate point to target
		FHitResult LOSHit;
		FCollisionQueryParams LOSParams;
		LOSParams.AddIgnoredActor(Data.Drone);
		LOSParams.AddIgnoredActor(Data.Target);

		const bool bLOSBlocked = Data.Drone->GetWorld()->LineTraceSingleByChannel(
			LOSHit,
			NewPoint,
			TargetLocation,
			ECC_Visibility,
			LOSParams
		);

		if (!bLOSBlocked)
		{
			// Point has LOS - use it
			Data.CurrentDestination = NewPoint;
			Data.bHasDestination = true;
			FlyingMovement->FlyToLocation(NewPoint, Data.AcceptanceRadius);
			return true;
		}
	}

	// No LOS-valid point found
	if (!bCurrentlyHasLOS)
	{
		// No LOS currently - try a point closer to target to approach
		FVector ApproachPoint;
		const float ApproachRadius = Data.OrbitRadius * 0.4f;
		if (FlyingMovement->GetRandomPointInVolume(TargetLocation, ApproachRadius, Data.MinHeight, Data.MaxHeight, ApproachPoint))
		{
			Data.CurrentDestination = ApproachPoint;
			Data.bHasDestination = true;
			FlyingMovement->FlyToLocation(ApproachPoint, Data.AcceptanceRadius);
			return true;
		}
	}

	// Use fallback point to keep moving
	if (bHasFallback)
	{
		Data.CurrentDestination = FallbackPoint;
		Data.bHasDestination = true;
		FlyingMovement->FlyToLocation(FallbackPoint, Data.AcceptanceRadius);
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

// ============================================================================
// RunAndShoot
// ============================================================================

EStateTreeRunStatus FSTTask_RunAndShoot::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC || !Data.Controller || !Data.Target)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (Data.NPC->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Reset state
	Data.bHasDestination = false;
	Data.bIsShooting = false;
	Data.CurrentDestination = FVector::ZeroVector;
	Data.LastLOSTime = Data.NPC->GetWorld()->GetTimeSeconds();

	// Set focus on target for strafing
	Data.Controller->SetFocus(Data.Target);

	// Pick first destination
	if (!PickNewDestination(Data))
	{
		UE_LOG(LogTemp, Warning, TEXT("RunAndShoot: Failed to pick initial destination"));
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_RunAndShoot::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC || Data.NPC->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!Data.Target || !Data.Controller)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Update focus to track moving target
	Data.Controller->SetFocus(Data.Target);

	// Track LOS status for repositioning
	const bool bHasLOS = Data.NPC->HasLineOfSightTo(Data.Target);
	const float CurrentTime = Data.NPC->GetWorld()->GetTimeSeconds();

	if (bHasLOS)
	{
		Data.LastLOSTime = CurrentTime;
	}

	// Check if we reached destination and pick new one
	if (Data.bHasDestination)
	{
		const FVector NPCLocation = Data.NPC->GetActorLocation();
		const float DistanceToDestination = FVector::Dist(NPCLocation, Data.CurrentDestination);

		// Check PathFollowingComponent status
		bool bNeedsNewDestination = DistanceToDestination <= Data.AcceptanceRadius;

		if (UPathFollowingComponent* PathComp = Data.Controller->GetPathFollowingComponent())
		{
			if (PathComp->DidMoveReachGoal() || PathComp->GetStatus() == EPathFollowingStatus::Idle)
			{
				bNeedsNewDestination = true;
			}
		}

		// No LOS for too long — interrupt current path to find a position with LOS
		if (!bHasLOS && (CurrentTime - Data.LastLOSTime) > RunAndShoot_LOSLostRepositionTime)
		{
			Data.Controller->StopMovement();
			bNeedsNewDestination = true;

			// Reset timer so the NPC has time to reach the new destination
			// before we force another reposition
			Data.LastLOSTime = CurrentTime;
		}

		if (bNeedsNewDestination)
		{
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
		// Currently shooting - check if LOS was lost mid-burst
		if (!Data.NPC->HasLineOfSightTo(Data.Target))
		{
			// LOS lost - stop shooting immediately to avoid firing through walls
			StopShooting(Data);
		}
		else if (Data.NPC->IsInBurstCooldown())
		{
			// Burst finished, entering cooldown
			Data.bIsShooting = false;

			// Release coordinator permission during cooldown
			if (Data.bUseCoordinator)
			{
				if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC))
				{
					Coordinator->NotifyAttackComplete(Data.NPC);
				}
			}
		}
		else if (!Data.NPC->IsCurrentlyShooting())
		{
			// Stopped shooting for other reason (interrupted, etc.)
			StopShooting(Data);
		}
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_RunAndShoot::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.NPC)
	{
		// Stop shooting
		if (Data.bIsShooting)
		{
			StopShooting(Data);
		}
	}

	if (Data.Controller)
	{
		// Stop movement
		Data.Controller->StopMovement();
		Data.Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

bool FSTTask_RunAndShoot::PickNewDestination(FInstanceDataType& Data) const
{
	if (!Data.NPC || !Data.Target || !Data.Controller)
	{
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Data.NPC->GetWorld());
	if (!NavSys)
	{
		return false;
	}

	const FVector TargetLocation = Data.Target->GetActorLocation();
	const FVector NPCLocation = Data.NPC->GetActorLocation();

	// Check if we currently have LOS - if not, prioritize finding a LOS-valid position
	const bool bCurrentlyHasLOS = Data.NPC->HasLineOfSightTo(Data.Target);

	// Try multiple times to find a valid point (prefer points with LOS)
	constexpr int32 MaxAttempts = 15;
	FNavLocation NavResult;
	FNavLocation BestNoLOSResult;
	bool bHasNoLOSFallback = false;

	for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	{
		// Search around target within MaxDistanceFromTarget
		if (NavSys->GetRandomReachablePointInRadius(TargetLocation, Data.MaxDistanceFromTarget, NavResult))
		{
			const float DistToTarget = FVector::Dist(NavResult.Location, TargetLocation);

			// Check minimum distance from target
			if (DistToTarget < Data.MinDistanceFromTarget)
			{
				continue;
			}

			// Check maximum distance from target
			if (DistToTarget > Data.MaxDistanceFromTarget)
			{
				continue;
			}

			// Check LOS from candidate point to target (eye height offset)
			FHitResult LOSHit;
			FCollisionQueryParams LOSParams;
			LOSParams.AddIgnoredActor(Data.NPC);
			LOSParams.AddIgnoredActor(Data.Target);

			const FVector EyeOffset(0.0f, 0.0f, 80.0f);
			const bool bLOSBlocked = Data.NPC->GetWorld()->LineTraceSingleByChannel(
				LOSHit,
				NavResult.Location + EyeOffset,
				TargetLocation,
				ECC_Visibility,
				LOSParams
			);

			if (!bLOSBlocked)
			{
				// Point has LOS to target - use it!
				Data.CurrentDestination = NavResult.Location;
				Data.bHasDestination = true;

				FAIMoveRequest MoveRequest;
				MoveRequest.SetGoalLocation(NavResult.Location);
				MoveRequest.SetAcceptanceRadius(Data.AcceptanceRadius);
				MoveRequest.SetUsePathfinding(true);
				MoveRequest.SetAllowPartialPath(true);
				MoveRequest.SetProjectGoalLocation(true);
				MoveRequest.SetCanStrafe(true);

				Data.Controller->MoveTo(MoveRequest);
				return true;
			}

			// No LOS but valid distance - save as fallback
			if (!bHasNoLOSFallback)
			{
				BestNoLOSResult = NavResult;
				bHasNoLOSFallback = true;
			}
		}
	}

	// No LOS-valid point found - move closer to target to regain LOS
	if (!bCurrentlyHasLOS)
	{
		// Try to find a point closer to target (within min distance) to approach
		for (int32 Attempt = 0; Attempt < 5; ++Attempt)
		{
			if (NavSys->GetRandomReachablePointInRadius(TargetLocation, Data.MinDistanceFromTarget, NavResult))
			{
				Data.CurrentDestination = NavResult.Location;
				Data.bHasDestination = true;

				FAIMoveRequest MoveRequest;
				MoveRequest.SetGoalLocation(NavResult.Location);
				MoveRequest.SetAcceptanceRadius(Data.AcceptanceRadius);
				MoveRequest.SetUsePathfinding(true);
				MoveRequest.SetAllowPartialPath(true);
				MoveRequest.SetProjectGoalLocation(true);
				MoveRequest.SetCanStrafe(true);

				Data.Controller->MoveTo(MoveRequest);
				return true;
			}
		}
	}

	// Fall back to any valid point (even without LOS) to keep moving
	if (bHasNoLOSFallback)
	{
		Data.CurrentDestination = BestNoLOSResult.Location;
		Data.bHasDestination = true;

		FAIMoveRequest MoveRequest;
		MoveRequest.SetGoalLocation(BestNoLOSResult.Location);
		MoveRequest.SetAcceptanceRadius(Data.AcceptanceRadius);
		MoveRequest.SetUsePathfinding(true);
		MoveRequest.SetAllowPartialPath(true);
		MoveRequest.SetProjectGoalLocation(true);
		MoveRequest.SetCanStrafe(true);

		Data.Controller->MoveTo(MoveRequest);
		return true;
	}

	return false;
}

bool FSTTask_RunAndShoot::CanShoot(const FInstanceDataType& Data) const
{
	if (!Data.NPC || !Data.Target)
	{
		return false;
	}

	// Don't shoot if dead
	if (Data.NPC->IsDead())
	{
		return false;
	}

	// Don't shoot if in burst cooldown
	if (Data.NPC->IsInBurstCooldown())
	{
		return false;
	}

	// Don't shoot if already shooting
	if (Data.NPC->IsCurrentlyShooting())
	{
		return false;
	}

	// Check line of sight
	if (!Data.NPC->HasLineOfSightTo(Data.Target))
	{
		return false;
	}

	// Check coordinator permission if needed
	if (Data.bUseCoordinator)
	{
		AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
		if (Coordinator && !Coordinator->RequestAttackPermission(Data.NPC))
		{
			return false;
		}
	}

	return true;
}

void FSTTask_RunAndShoot::StartShooting(FInstanceDataType& Data) const
{
	if (!Data.NPC || !Data.Target)
	{
		return;
	}

	// Start shooting (with external permission since we already checked coordinator)
	Data.NPC->StartShooting(Data.Target, true);
	Data.bIsShooting = true;

	// Notify coordinator that attack started
	if (Data.bUseCoordinator)
	{
		if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC))
		{
			Coordinator->NotifyAttackStarted(Data.NPC);
		}
	}
}

void FSTTask_RunAndShoot::StopShooting(FInstanceDataType& Data) const
{
	if (!Data.NPC)
	{
		return;
	}

	Data.NPC->StopShooting();
	Data.bIsShooting = false;

	// Notify coordinator that attack completed
	if (Data.bUseCoordinator)
	{
		if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC))
		{
			Coordinator->NotifyAttackComplete(Data.NPC);
		}
	}
}

#if WITH_EDITOR
FText FSTTask_RunAndShoot::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	if (Data)
	{
		return FText::Format(NSLOCTEXT("PolarityAI", "RunAndShootDesc",
			"Run around target ({0}-{1}) and shoot when ready"),
			FText::AsNumber(static_cast<int32>(Data->MinDistanceFromTarget)),
			FText::AsNumber(static_cast<int32>(Data->MaxDistanceFromTarget)));
	}
	return NSLOCTEXT("PolarityAI", "RunAndShootDescDefault", "Run around target and shoot when ready");
}
#endif

// ============================================================================
// GetRandomNavPoint
// ============================================================================

EStateTreeRunStatus FSTTask_GetRandomNavPoint::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	Data.bFoundPoint = false;
	Data.RandomPoint = FVector::ZeroVector;

	if (!Data.Pawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("GetRandomNavPoint: No Pawn!"));
		return EStateTreeRunStatus::Failed;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Data.Pawn->GetWorld());
	if (!NavSys)
	{
		UE_LOG(LogTemp, Warning, TEXT("GetRandomNavPoint: No NavSystem!"));
		return EStateTreeRunStatus::Failed;
	}

	const FVector PawnLocation = Data.Pawn->GetActorLocation();
	const bool bHasTarget = IsValid(Data.Target);
	const FVector TargetLocation = bHasTarget ? Data.Target->GetActorLocation() : FVector::ZeroVector;

	// If we have a target, search around the TARGET (not pawn) within SearchRadius
	// This ensures we find points that are actually near combat range
	const FVector SearchOrigin = bHasTarget ? TargetLocation : PawnLocation;
	const float EffectiveSearchRadius = bHasTarget ? Data.MaxDistanceFromTarget : Data.SearchRadius;

	// Try multiple times to find a valid point
	constexpr int32 MaxAttempts = 15;
	FNavLocation NavResult;

	for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	{
		// Get a random reachable point around the search origin
		if (NavSys->GetRandomReachablePointInRadius(SearchOrigin, EffectiveSearchRadius, NavResult))
		{
			// If we have a target, verify distance constraints
			if (bHasTarget)
			{
				const float DistToTarget = FVector::Dist(NavResult.Location, TargetLocation);

				// Check minimum distance from target
				if (DistToTarget < Data.MinDistanceFromTarget)
				{
					continue; // Too close to target, try again
				}

				// Check maximum distance from target
				if (DistToTarget > Data.MaxDistanceFromTarget)
				{
					continue; // Too far from target, try again
				}

				// Also check that the point is reachable from pawn's current location
				// (the point should be on connected navmesh)
				const float DistFromPawn = FVector::Dist(NavResult.Location, PawnLocation);

				// Skip points that are too far from current position (would take too long to reach)
				if (DistFromPawn > Data.MaxDistanceFromTarget * 2.0f)
				{
					continue;
				}
			}

			// Valid point found!
			Data.RandomPoint = NavResult.Location;
			Data.bFoundPoint = true;

			UE_LOG(LogTemp, Log, TEXT("GetRandomNavPoint: Found point at %s (dist to target: %.0f, dist from pawn: %.0f)"),
				*NavResult.Location.ToString(),
				bHasTarget ? FVector::Dist(NavResult.Location, TargetLocation) : 0.0f,
				FVector::Dist(NavResult.Location, PawnLocation));

			return EStateTreeRunStatus::Succeeded;
		}
	}

	// Failed to find a valid point - fall back to current location (don't move)
	UE_LOG(LogTemp, Warning, TEXT("GetRandomNavPoint: Failed to find valid point after %d attempts! Pawn: %s, Target: %s"),
		MaxAttempts,
		*PawnLocation.ToString(),
		bHasTarget ? *TargetLocation.ToString() : TEXT("None"));

	// Return current pawn location as fallback so movement doesn't fail completely
	Data.RandomPoint = PawnLocation;
	Data.bFoundPoint = true;
	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FSTTask_GetRandomNavPoint::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	if (Data)
	{
		return FText::Format(NSLOCTEXT("PolarityAI", "GetRandomNavPointDesc",
			"Get random nav point (radius: {0})"), FText::AsNumber(static_cast<int32>(Data->SearchRadius)));
	}
	return NSLOCTEXT("PolarityAI", "GetRandomNavPointDescDefault", "Get random navigable point");
}
#endif