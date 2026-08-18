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
#include "GameFramework/CharacterMovementComponent.h"
#include "../../ApexMovementComponent.h"

namespace
{
	/** Max horizontal speed (cm/s) at which a stop-and-shoot NPC is considered "planted" and may
	 *  open fire. Above this it keeps braking — the visible halt telegraphs the incoming shot. */
	constexpr float RunAndShoot_ShootMoveSpeedThreshold = 50.0f;

	/** Stop-and-shoot rotation toggle for ground shooters driven by RunAndShoot.
	 *  bFaceTarget == false → orient the body to its movement direction (the forward-run
	 *                          animation matches while repositioning).
	 *  bFaceTarget == true  → rotate the body toward the controller's focus/target (used while
	 *                          stopped to fire, so the NPC faces the player without needing a
	 *                          directional-strafe blendspace). */
	/** The spot on the duel ring this charge committed to, in world space right now.
	 *
	 *  The bearing is fixed for the whole charge; the point is not, because it hangs off the target
	 *  and the target walks. That is the entire trick behind following a moving player smoothly:
	 *  re-deriving the point from a stored angle moves it by exactly as much as the player moved,
	 *  whereas re-choosing a point on the ring would jump to a different side of them. */
	FVector ShooterPush_RingPoint(const FSTTask_ShooterPush_Data& Data)
	{
		FVector Point = Data.Target->GetActorLocation()
			+ FRotator(0.0f, Data.CommittedBearingDeg, 0.0f).Vector() * Data.DuelDistance;
		Point.Z = Data.NPC->GetActorLocation().Z;
		return Point;
	}

	/** Drop the sprint latch and enter the braking slide in one step. Separated only so the call site
	 *  reads as one decision: an NPC left sprinting into a slide keeps bSprintKeyHeld set, and the
	 *  latch outlives the slide exactly the way the crouch used to. */
	void Apex_StopSprintAndSlide(UApexMovementComponent* Apex, const FVector& Point, float SteerRateDeg)
	{
		if (!Apex)
		{
			return;
		}

		Apex->StopSprint();
		Apex->StartSlideToPoint(Point, SteerRateDeg);
	}

	void SetShooterRotationMode(AShooterNPC* NPC, bool bFaceTarget)
	{
		if (!NPC)
		{
			return;
		}
		if (UCharacterMovementComponent* CMC = NPC->GetCharacterMovement())
		{
			CMC->bUseControllerDesiredRotation = bFaceTarget;
			CMC->bOrientRotationToMovement = !bFaceTarget;
		}
	}
}

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

	// [NAV_DEBUG] Detailed navigation diagnostics
	{
		APawn* Pawn = Data.Controller->GetPawn();
		const FString PawnName = Pawn ? Pawn->GetName() : TEXT("NULL");
		const FVector PawnLoc = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;

		UE_LOG(LogTemp, Warning, TEXT("[NAV_DEBUG] %s MoveWithStrafe: Result=%d, From=%s, To=%s, UsePathfinding=%d"),
			*PawnName, static_cast<int32>(Result.Code), *PawnLoc.ToString(), *Data.Destination.ToString(), Data.bUsePathfinding);

		// Check NavMesh at both locations
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Data.Controller->GetWorld());
		if (NavSys)
		{
			FNavLocation NavLoc;
			const bool bStartOnNav = NavSys->ProjectPointToNavigation(PawnLoc, NavLoc, FVector(50, 50, 200));
			const bool bDestOnNav = NavSys->ProjectPointToNavigation(Data.Destination, NavLoc, FVector(50, 50, 200));
			UE_LOG(LogTemp, Warning, TEXT("[NAV_DEBUG] %s NavMesh check: StartOnNav=%d, DestOnNav=%d"),
				*PawnName, bStartOnNav, bDestOnNav);

			if (!bStartOnNav)
			{
				UE_LOG(LogTemp, Error, TEXT("[NAV_DEBUG] %s NPC LOCATION IS NOT ON NAVMESH! Loc=%s"), *PawnName, *PawnLoc.ToString());
			}
			if (!bDestOnNav)
			{
				UE_LOG(LogTemp, Error, TEXT("[NAV_DEBUG] %s DESTINATION IS NOT ON NAVMESH! Dest=%s"), *PawnName, *Data.Destination.ToString());
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[NAV_DEBUG] %s NO NAVIGATION SYSTEM FOUND!"), *PawnName);
		}
	}

	// Check immediate move result
	if (Result.Code == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NAV_DEBUG] MoveWithStrafe: MoveTo FAILED immediately!"));
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
			APawn* IdlePawn = Data.Controller->GetPawn();
			const float DistToGoal = IdlePawn ? FVector::Dist(IdlePawn->GetActorLocation(), Data.Destination) : -1.0f;
			if (IdlePawn && DistToGoal <= Data.AcceptanceRadius * 1.5f)
			{
				UE_LOG(LogTemp, Log, TEXT("MoveWithStrafe: Close enough to goal (dist=%.0f)"), DistToGoal);
				return EStateTreeRunStatus::Succeeded;
			}

			UE_LOG(LogTemp, Warning, TEXT("[NAV_DEBUG] %s MoveWithStrafe: PathFollowing is Idle - movement FAILED, dist=%.0f"),
				IdlePawn ? *IdlePawn->GetName() : TEXT("NULL"), DistToGoal);
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
			// Burst finished, entering cooldown — call StopShooting to prevent auto-resume.
			// OnBurstCooldownEnd() checks bWantsToShoot: if true, it auto-starts a new burst
			// via TryStartShooting() WITHOUT any LOS check, bypassing the task entirely.
			// StopShooting() clears bWantsToShoot, so the next burst will only start
			// when CanShoot() passes on a subsequent Tick (which includes LOS check).
			StopShooting(Data);
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

	// --- Battle Circle Integration ---
	if (Data.bUseCoordinator)
	{
		AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.Drone);
		if (Coordinator)
		{
			FVector SlotPosition;
			if (Coordinator->GetAssignedSlotPosition(Data.Drone, SlotPosition))
			{
				// Add vertical offset for flying drone
				const float HeightOffset = FMath::FRandRange(Data.MinHeight, Data.MaxHeight);
				SlotPosition.Z = Data.Target->GetActorLocation().Z + HeightOffset;

				Data.CurrentDestination = SlotPosition;
				Data.bHasDestination = true;
				FlyingMovement->FlyToLocation(SlotPosition, Data.AcceptanceRadius);
				return true;
			}
		}
	}
	// --- End Battle Circle Integration ---

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
	if (!Data.Drone || !Data.Target) return false;
	if (Data.Drone->IsDead()) return false;
	if (Data.Drone->IsInBurstCooldown()) return false;
	if (Data.Drone->IsCurrentlyShooting()) return false;
	if (!Data.Drone->HasLineOfSightTo(Data.Target)) return false;

	if (Data.bUseCoordinator)
	{
		AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.Drone);
		if (Coordinator && !Coordinator->RequestAttackPermission(Data.Drone))
		{
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
	Data.LastStuckCheckPosition = Data.NPC->GetActorLocation();
	Data.LastStuckCheckTime = Data.LastLOSTime;

	// Stop-and-shoot: begin in "moving" mode. While repositioning we orient the body to its
	// movement direction so the forward-run animation matches; we only turn to face the target
	// while stopped to fire (see StartShooting). Focus stays on the target for aiming.
	SetShooterRotationMode(Data.NPC, /*bFaceTarget*/ false);
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

	// Keep focus on the target for aiming (body rotation is governed by the move/aim mode toggled
	// in StartShooting / StopShooting).
	Data.Controller->SetFocus(Data.Target);

	// Track LOS status for repositioning
	const bool bHasLOS = Data.NPC->HasLineOfSightTo(Data.Target);
	const float CurrentTime = Data.NPC->GetWorld()->GetTimeSeconds();

	if (bHasLOS)
	{
		Data.LastLOSTime = CurrentTime;
	}

	// Stop-and-shoot decision, computed once. While firing OR planting a shot the NPC stands still
	// and faces the target; only otherwise does it reposition. (CanShoot has coordinator/LOS side
	// effects, so evaluate it a single time per Tick.)
	const bool bWantsToShoot = !Data.bIsShooting && CanShoot(Data);

	// While firing or planting a shot: stand still — no repositioning. Movement is halted in the
	// shooting block below / StartShooting, and the body faces the target.
	if (Data.bIsShooting || bWantsToShoot)
	{
		// Intentionally idle here: standing still, facing the target. The shooting block below
		// handles planting/firing and when to resume movement.
	}
	// While repositioning, make sure we always have a destination to move toward.
	else if (!Data.bHasDestination)
	{
		PickNewDestination(Data);
	}
	// Check if we reached destination and pick new one
	else if (Data.bHasDestination)
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

		// Stuck detection: if NPC hasn't moved significantly but should be,
		// force stop and reposition. Catches cases where PathFollowing thinks
		// it's "Moving" but the NPC is physically blocked.
		if (!bNeedsNewDestination && (CurrentTime - Data.LastStuckCheckTime) >= RunAndShoot_StuckCheckInterval)
		{
			const float DistanceMoved = FVector::Dist(NPCLocation, Data.LastStuckCheckPosition);
			Data.LastStuckCheckPosition = NPCLocation;
			Data.LastStuckCheckTime = CurrentTime;

			if (DistanceMoved < RunAndShoot_StuckDistanceThreshold)
			{
				Data.Controller->StopMovement();
				bNeedsNewDestination = true;
			}
		}

		if (bNeedsNewDestination)
		{
			PickNewDestination(Data);
		}
	}

	// Handle shooting.
	if (bWantsToShoot)
	{
		// Plant first, fire second. Halt + face the target now, but only open fire once we're
		// nearly stopped, so the visible halt telegraphs the shot instead of the NPC firing while
		// still sliding.
		Data.Controller->StopMovement();
		Data.Controller->SetFocus(Data.Target);
		Data.bHasDestination = false;
		SetShooterRotationMode(Data.NPC, /*bFaceTarget*/ true);

		if (Data.NPC->GetVelocity().Size2D() <= RunAndShoot_ShootMoveSpeedThreshold)
		{
			StartShooting(Data);
		}
	}
	else if (Data.bIsShooting)
	{
		// Currently shooting - check if LOS was lost mid-burst
		if (!Data.NPC->HasLineOfSightTo(Data.Target))
		{
			// LOS lost - stop shooting immediately to avoid firing through walls
			StopShooting(Data);
		}
		else if (Data.NPC->IsInBurstCooldown())
		{
			// Burst finished, entering cooldown — call StopShooting to prevent auto-resume.
			// OnBurstCooldownEnd() checks bWantsToShoot: if true, it auto-starts a new burst
			// via TryStartShooting() WITHOUT any LOS check, bypassing the task entirely.
			// StopShooting() clears bWantsToShoot, so the next burst will only start
			// when CanShoot() passes on a subsequent Tick (which includes LOS check).
			StopShooting(Data);
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

		// Restore the default "face target" rotation mode (matches AShooterNPC::BeginPlay) so a
		// following state doesn't inherit the reposition (orient-to-movement) mode.
		SetShooterRotationMode(Data.NPC, /*bFaceTarget*/ true);
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

	const FVector NPCLocationForStuck = Data.NPC->GetActorLocation();

	// Helper lambda to issue MoveTo and return success (defined early for battle circle use)
	auto TryMoveToSlot = [&Data, &NPCLocationForStuck](const FVector& GoalLocation) -> bool
	{
		FAIMoveRequest MoveRequest;
		MoveRequest.SetGoalLocation(GoalLocation);
		MoveRequest.SetAcceptanceRadius(Data.AcceptanceRadius);
		MoveRequest.SetUsePathfinding(true);
		MoveRequest.SetAllowPartialPath(true);
		MoveRequest.SetProjectGoalLocation(true);
		MoveRequest.SetCanStrafe(true);

		const FPathFollowingRequestResult Result = Data.Controller->MoveTo(MoveRequest);
		if (Result.Code == EPathFollowingRequestResult::Failed)
		{
			return false;
		}

		Data.CurrentDestination = GoalLocation;
		Data.bHasDestination = true;
		Data.LastStuckCheckPosition = NPCLocationForStuck;
		Data.LastStuckCheckTime = Data.NPC->GetWorld()->GetTimeSeconds();
		return true;
	};

	// --- Battle Circle Integration ---
	if (Data.bUseCoordinator)
	{
		AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
		if (Coordinator)
		{
			FVector SlotPosition;
			if (Coordinator->GetAssignedSlotPosition(Data.NPC, SlotPosition))
			{
				UNavigationSystemV1* NavSysSlot = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Data.NPC->GetWorld());
				if (NavSysSlot)
				{
					FNavLocation NavResult;
					if (NavSysSlot->ProjectPointToNavigation(SlotPosition, NavResult, FVector(200.0f, 200.0f, 200.0f)))
					{
						if (TryMoveToSlot(NavResult.Location))
						{
							return true;
						}
					}
				}
				// Fallback: try moving directly to slot position
				if (TryMoveToSlot(SlotPosition))
				{
					return true;
				}
			}
		}
	}
	// --- End Battle Circle Integration ---

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Data.NPC->GetWorld());
	if (!NavSys)
	{
		return false;
	}

	const FVector TargetLocation = Data.Target->GetActorLocation();
	const FVector NPCLocation = Data.NPC->GetActorLocation();

	// Check if we currently have LOS - if not, prioritize finding a LOS-valid position
	const bool bCurrentlyHasLOS = Data.NPC->HasLineOfSightTo(Data.Target);

	// Helper lambda to issue MoveTo and return success
	auto TryMoveTo = [&Data, &NPCLocation](const FVector& GoalLocation) -> bool
	{
		FAIMoveRequest MoveRequest;
		MoveRequest.SetGoalLocation(GoalLocation);
		MoveRequest.SetAcceptanceRadius(Data.AcceptanceRadius);
		MoveRequest.SetUsePathfinding(true);
		MoveRequest.SetAllowPartialPath(true);
		MoveRequest.SetProjectGoalLocation(true);
		MoveRequest.SetCanStrafe(true);

		const FPathFollowingRequestResult Result = Data.Controller->MoveTo(MoveRequest);
		if (Result.Code == EPathFollowingRequestResult::Failed)
		{
			return false;
		}

		Data.CurrentDestination = GoalLocation;
		Data.bHasDestination = true;

		// Reset stuck detection so NPC has full interval to reach new destination
		Data.LastStuckCheckPosition = NPCLocation;
		Data.LastStuckCheckTime = Data.NPC->GetWorld()->GetTimeSeconds();
		return true;
	};

	// Try multiple times to find a valid point (prefer points with LOS)
	constexpr int32 MaxAttempts = 15;
	FNavLocation NavResult;
	FNavLocation BestNoLOSResult;
	bool bHasNoLOSFallback = false;
	FNavLocation AnyValidResult;
	bool bHasAnyValid = false;

	for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	{
		// Search around target within MaxDistanceFromTarget
		if (NavSys->GetRandomReachablePointInRadius(TargetLocation, Data.MaxDistanceFromTarget, NavResult))
		{
			// Save first valid navmesh point as universal fallback (before distance checks)
			if (!bHasAnyValid)
			{
				AnyValidResult = NavResult;
				bHasAnyValid = true;
			}

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
				if (TryMoveTo(NavResult.Location))
				{
					return true;
				}
				// MoveTo failed (path unreachable) - keep searching
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
				if (TryMoveTo(NavResult.Location))
				{
					return true;
				}
			}
		}
	}

	// Fall back to any valid point with correct distance (even without LOS) to keep moving
	if (bHasNoLOSFallback)
	{
		if (TryMoveTo(BestNoLOSResult.Location))
		{
			return true;
		}
	}

	// Fall back to any valid navmesh point found around target
	if (bHasAnyValid)
	{
		if (TryMoveTo(AnyValidResult.Location))
		{
			return true;
		}
	}

	// Last resort: move directly towards the target actor.
	// This handles the case where the NPC is very far from the player
	// and random points around the player are unreachable.
	{
		FAIMoveRequest MoveRequest;
		MoveRequest.SetGoalActor(Data.Target);
		MoveRequest.SetAcceptanceRadius(Data.MinDistanceFromTarget);
		MoveRequest.SetUsePathfinding(true);
		MoveRequest.SetAllowPartialPath(true);
		MoveRequest.SetProjectGoalLocation(true);
		MoveRequest.SetCanStrafe(true);

		const FPathFollowingRequestResult Result = Data.Controller->MoveTo(MoveRequest);
		if (Result.Code != EPathFollowingRequestResult::Failed)
		{
			Data.CurrentDestination = TargetLocation;
			Data.bHasDestination = true;
			return true;
		}
	}

	return false;
}

bool FSTTask_RunAndShoot::CanShoot(const FInstanceDataType& Data) const
{
	if (!Data.NPC || !Data.Target) return false;
	if (Data.NPC->IsDead()) return false;
	if (Data.NPC->IsInBurstCooldown()) return false;
	if (Data.NPC->IsCurrentlyShooting()) return false;
	if (!Data.NPC->HasLineOfSightTo(Data.Target)) return false;

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

	// Stop-and-shoot: halt and turn to face the target before firing. The stop telegraphs the
	// incoming shot, and standing still lets the body face the player without the strafe-animation
	// mismatch (these NPCs have no directional-strafe locomotion).
	if (Data.Controller)
	{
		Data.Controller->StopMovement();
		Data.Controller->SetFocus(Data.Target);
	}
	Data.bHasDestination = false;
	SetShooterRotationMode(Data.NPC, /*bFaceTarget*/ true);

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

	// Back to repositioning: orient the body to movement so the run animation matches again,
	// and force a fresh destination to be picked on the next Tick.
	SetShooterRotationMode(Data.NPC, /*bFaceTarget*/ false);
	Data.bHasDestination = false;

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


// ============================================================================
// ShooterPush
// ============================================================================

EStateTreeRunStatus FSTTask_ShooterPush::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC || !Data.Controller || !Data.Target || !Data.NPC->GetWorld())
	{
		return EStateTreeRunStatus::Failed;
	}

	const float Now = Data.NPC->GetWorld()->GetTimeSeconds();

	Data.Phase = ResolvePhase(Data);
	Data.PreviousPhase = Data.Phase;
	Data.bWithdrawing = false;
	Data.DuelEnteredTime = Now;
	Data.bHasLeg = false;
	Data.bIsShooting = false;
	Data.LegSign = FMath::RandBool() ? 1.0f : -1.0f;
	Data.LastStuckCheckPosition = Data.NPC->GetActorLocation();
	Data.LastStuckCheckTime = Now;
	Data.bHasCommittedBearing = false;
	Data.LastRetargetTime = Now;
	Data.bChargeArrived = false;
	Data.bWasSlidingToPoint = false;

	Data.Controller->SetFocus(Data.Target);
	SetShooterRotationMode(Data.NPC, /*bFaceTarget*/ true);

	StartLeg(Data);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_ShooterPush::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
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

	UWorld* const World = Data.NPC->GetWorld();
	if (!World)
	{
		return EStateTreeRunStatus::Failed;
	}

	const float Now = World->GetTimeSeconds();
	const FVector NPCLocation = Data.NPC->GetActorLocation();
	const float Distance = FVector::Dist2D(NPCLocation, Data.Target->GetActorLocation());

	// Aim and facing are re-asserted every tick rather than only on entry, because two different
	// things drift out from under them. The sensed target can be swapped while this state stays
	// active, leaving focus on whoever was there at EnterState; and the rotation flags are plain
	// bools that anything else touching the movement component can flip. Both look the same on
	// screen: an enemy firing at the player while its body points somewhere else.
	//
	// Aim follows the target unconditionally - having a target and not aiming at it is never
	// correct. Body rotation is the one exception, and only for the sprint; it is asserted below,
	// once the phase for this tick is known.
	if (Data.Controller->GetFocusActor() != Data.Target)
	{
		Data.Controller->SetFocus(Data.Target);
	}

	// Did the braking slide just finish? Asked here, before the phase is resolved, so the duel opens
	// on the very tick the NPC arrives instead of the one after it.
	{
		const UApexMovementComponent* const ApexArrival =
			Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement());
		const bool bSlidingToPoint = ApexArrival && ApexArrival->IsSlidingToPoint();

		if (Data.bWasSlidingToPoint && !bSlidingToPoint)
		{
			Data.bChargeArrived = true;
		}
		Data.bWasSlidingToPoint = bSlidingToPoint;
	}

	// The duel clock. Rotation is the whole reason an enemy in your face is a moment rather than a
	// condition, so it runs off wall time from entry, not off anything the NPC achieves.
	if (Data.Phase == EShooterPushPhase::Duel && !Data.bWithdrawing
		&& (Now - Data.DuelEnteredTime) >= Data.DuelDuration)
	{
		Data.bWithdrawing = true;

		// The arrival latch belongs to the charge that just ended. Leaving it set would hold the
		// next cycle in the duel the moment the withdrawal cleared.
		Data.bChargeArrived = false;
	}

	// Far enough out: the push is over, the token goes back, and the distance bands take over again.
	if (Data.bWithdrawing && Distance >= Data.WithdrawDistance)
	{
		Data.bWithdrawing = false;
		Data.DuelEnteredTime = Now;

		if (AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC))
		{
			Coordinator->NotifyAttackComplete(Data.NPC);
		}
	}

	const EShooterPushPhase PhaseBefore = Data.PreviousPhase;
	Data.Phase = ResolvePhase(Data);

	// ---- Phase edges ----
	if (Data.Phase != Data.PreviousPhase)
	{
		UApexMovementComponent* const Apex = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement());

		// The slide is NOT fired here any more. As an event on the Sprint -> Duel boundary it was
		// structurally too late: the boundary is crossed at duel range, so the entire slide happened
		// on the far side of the ring and carried the NPC straight through it. It now starts
		// SlideLeadTime before arrival, from the sprint block below, and brakes onto the spot.
		if (Apex && Data.PreviousPhase == EShooterPushPhase::Sprint)
		{
			Apex->StopSprint();
		}

		// Leaving the sprint drops the commitment so the next charge picks its own side.
		if (Data.PreviousPhase == EShooterPushPhase::Sprint && Data.Phase != EShooterPushPhase::Sprint)
		{
			Data.bHasCommittedBearing = false;
		}

		if (Apex && Data.Phase == EShooterPushPhase::Sprint)
		{
			Apex->StartSprint();
		}

		if (Data.Phase == EShooterPushPhase::Duel)
		{
			Data.DuelEnteredTime = Now;
		}

		// A leg computed for the old phase points the wrong way for the new one.
		Data.bHasLeg = false;
		Data.PreviousPhase = Data.Phase;
	}

	// Body rotation, every tick and not just on the phase edge, for the same drift reason as the
	// focus above.
	//
	// The exception is narrower than "the sprint": it is the sprint ON FOOT. A charge turns to face
	// where it is running because that is the action the run animation describes, but the slide at
	// the end of it is the arrival, not the run, and by then the gun should already be pointed at
	// the player. Leaving the body on the movement direction through the slide also swung the sight
	// cone off the player right at the moment of closing, which is where the lost targets were
	// coming from.
	//
	// Movement shape does not depend on this either way: the run is a MoveTo goal with bCanStrafe,
	// and the slide is driven by Velocity, so neither one reads the body's facing.
	const UApexMovementComponent* const ApexRotation = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement());
	const bool bChargingOnFoot = Data.Phase == EShooterPushPhase::Sprint
		&& !(ApexRotation && ApexRotation->IsSliding());

	SetShooterRotationMode(Data.NPC, /*bFaceTarget*/ !bChargingOnFoot);

	// ---- The charge, while it is running ----
	//
	// Three things happen here and nowhere else, because all three depend on where the committed
	// spot is RIGHT NOW rather than where it was when the leg was issued.
	if (Data.Phase == EShooterPushPhase::Sprint && Data.bHasCommittedBearing)
	{
		UApexMovementComponent* const ApexRun = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement());
		const FVector RingPoint = ShooterPush_RingPoint(Data);

		if (ApexRun && ApexRun->IsSlidingToPoint())
		{
			// Already sliding: hand it the spot every frame. The braking distance and the steering
			// are recomputed from it inside the movement simulation, so a player who backs off
			// stretches the slide and one who steps in shortens it.
			ApexRun->UpdateSlideTargetPoint(RingPoint);
		}
		else if (ApexRun && !ApexRun->IsSliding())
		{
			// Commit to the slide by TIME to arrival, not by crossing a distance. Time is what makes
			// this survive retuning: SlideLeadTime seconds of running is the same fraction of the
			// approach whether the NPC covers 400 or 800 units a second.
			const float SpeedNow = ApexRun->Velocity.Size2D();
			const float ToPoint = FVector::Dist2D(NPCLocation, RingPoint);

			if (Data.SlideLeadTime > 0.0f && SpeedNow > KINDA_SMALL_NUMBER
				&& ToPoint <= SpeedNow * Data.SlideLeadTime)
			{
				Apex_StopSprintAndSlide(ApexRun, RingPoint, Data.SlideSteerRateDeg);
			}
			else if ((Now - Data.LastRetargetTime) >= Data.SprintRetargetInterval
				&& FVector::Dist2D(RingPoint, Data.LegDestination) > Data.SprintRetargetTolerance)
			{
				// The spot has drifted far enough from the goal the path was built for. Re-issue,
				// but on a throttle: the point moves continuously and the path request must not.
				Data.LastRetargetTime = Now;
				Data.bHasLeg = false;
			}
		}
	}

	// The crouch is a key state, exactly like the sprint latch, and it has the same problem: the
	// slide switches it ON (StartSlide -> StartCrouching) and only the player's crouch key release
	// (StopCrouchSlide) ever switches it OFF. An NPC has no key, so every slide left it walking
	// around crouched for the rest of the fight. Release it as soon as the slide it belongs to is
	// over; while sliding it must stay, because that is the slide's own pose.
	if (UApexMovementComponent* const ApexCrouch = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement()))
	{
		if (!ApexCrouch->IsSliding() && ApexCrouch->IsCrouching())
		{
			ApexCrouch->StopCrouching();
		}
	}

	// ---- Diagnostics ----
	// Temporary, and deliberately reporting measurements rather than conclusions: the two complaints
	// this has to settle are "the body does not stay locked on the player" and "it never leaves the
	// approach", and both are unfalsifiable from watching. Filter the Output Log on [AI_DEBUG].
	// Throttled off world time so no new instance-data field (and therefore no header change) is
	// needed; the phase edge always logs.
	{
		const bool bPhaseEdge = Data.Phase != PhaseBefore;
		if (bPhaseEdge || FMath::Fmod(Now, 0.5f) < DeltaTime)
		{
			UCharacterMovementComponent* const CMC = Data.NPC->GetCharacterMovement();
			const UApexMovementComponent* const Apex = Cast<UApexMovementComponent>(CMC);

			const FVector ToTarget = (Data.Target->GetActorLocation() - NPCLocation).GetSafeNormal2D();
			const float YawError = ToTarget.IsNearlyZero() ? -1.0f
				: FMath::RadiansToDegrees(FMath::Acos(
					FMath::Clamp(FVector::DotProduct(Data.NPC->GetActorForwardVector().GetSafeNormal2D(), ToTarget), -1.0f, 1.0f)));

			const AActor* const Focus = Data.Controller->GetFocusActor();

			UE_LOG(LogTemp, Warning, TEXT("[AI_DEBUG] %s phase=%d dist=%.0f (sprint@%.0f duel@%.0f) ")
				TEXT("yawErr=%.1f focus=%s target=%s ctrlRot=%d orientMove=%d rotRate=%.0f ")
				TEXT("speed=%.0f maxSpeed=%.0f hasMoveSettings=%d sprinting=%d sliding=%d los=%d withdraw=%d"),
				*Data.NPC->GetName(),
				static_cast<int32>(Data.Phase),
				Distance, Data.SprintDistance, Data.DuelDistance,
				YawError,
				Focus ? *Focus->GetName() : TEXT("NULL"),
				*Data.Target->GetName(),
				CMC ? static_cast<int32>(CMC->bUseControllerDesiredRotation) : 0,
				CMC ? static_cast<int32>(CMC->bOrientRotationToMovement) : 0,
				CMC ? CMC->RotationRate.Yaw : 0.0f,
				CMC ? CMC->Velocity.Size2D() : 0.0f,
				CMC ? CMC->GetMaxSpeed() : 0.0f,
				(Apex && Apex->MovementSettings) ? 1 : 0,
				Apex ? static_cast<int32>(Apex->IsSprinting()) : 0,
				Apex ? static_cast<int32>(Apex->IsSliding()) : 0,
				Data.NPC->HasLineOfSightTo(Data.Target) ? 1 : 0,
				Data.bWithdrawing ? 1 : 0);
		}
	}

	// ---- Legs ----
	bool bNeedsNewLeg = !Data.bHasLeg || Now >= Data.LegEndTime;

	if (!bNeedsNewLeg)
	{
		if (UPathFollowingComponent* PathComp = Data.Controller->GetPathFollowingComponent())
		{
			if (PathComp->DidMoveReachGoal() || PathComp->GetStatus() == EPathFollowingStatus::Idle)
			{
				bNeedsNewLeg = true;
			}
		}
	}

	// Stuck: a diagonal leg is exactly the trajectory that walks into a corner, so this is not
	// paranoia. Abandoning the leg flips the sign and sends it the other way.
	if (!bNeedsNewLeg && (Now - Data.LastStuckCheckTime) >= ShooterPush_StuckCheckInterval)
	{
		const float Moved = FVector::Dist(NPCLocation, Data.LastStuckCheckPosition);
		Data.LastStuckCheckPosition = NPCLocation;
		Data.LastStuckCheckTime = Now;

		if (Moved < ShooterPush_StuckDistanceThreshold)
		{
			Data.Controller->StopMovement();
			bNeedsNewLeg = true;
		}
	}

	if (bNeedsNewLeg)
	{
		StartLeg(Data);
	}

	// ---- Fire ----
	// Note what is NOT here: no StopMovement, no waiting for the velocity to fall. Firing happens on
	// the move, and the price is paid in spread through the self-movement term in
	// UAIAccuracyComponent.
	bool bWantsFire = false;
	switch (Data.Phase)
	{
	case EShooterPushPhase::Approach:
	case EShooterPushPhase::Duel:
		bWantsFire = true;
		break;
	case EShooterPushPhase::Withdraw:
		bWantsFire = Data.bWithdrawLegFires;
		break;
	case EShooterPushPhase::Sprint:
	default:
		bWantsFire = false;
		break;
	}

	if (bWantsFire && Data.NPC->HasLineOfSightTo(Data.Target))
	{
		if (!Data.bIsShooting)
		{
			StartShooting(Data);
		}
		else if (Data.NPC->IsInBurstCooldown() || !Data.NPC->IsCurrentlyShooting())
		{
			// Same trap the old task documented: the NPC restarts its own burst when the cooldown
			// ends without re-checking line of sight, so the task has to close the request
			// explicitly and let the next tick reopen it.
			StopShooting(Data);
		}
	}
	else if (Data.bIsShooting)
	{
		StopShooting(Data);
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ShooterPush::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	ReleaseMovementState(Data);
}

/** Dead band on the distance thresholds. Without one, a target sitting on a boundary flips the phase
 *  every frame; measured 291 phase changes in 630 samples before this existed. */
static constexpr float ShooterPush_PhaseHysteresis = 150.0f;


EShooterPushPhase FSTTask_ShooterPush::ResolvePhase(const FInstanceDataType& Data) const
{
	if (Data.bWithdrawing)
	{
		return EShooterPushPhase::Withdraw;
	}

	// The charge arrived on its committed spot, so the duel starts now - before the distance test
	// gets a say. The braking slide stops within the arrival tolerance of the ring rather than
	// exactly on it, which left the NPC a few dozen centimetres outside DuelDistance, still
	// formally sprinting, having to stand up and walk in before anything happened. That gap was the
	// pause between the slide and the duel.
	if (Data.bChargeArrived)
	{
		return EShooterPushPhase::Duel;
	}

	if (!Data.NPC || !Data.Target)
	{
		return EShooterPushPhase::Approach;
	}

	const float Distance = FVector::Dist2D(Data.NPC->GetActorLocation(), Data.Target->GetActorLocation());

	// "Phase is a function of distance" was the right decision and it stays; what it was missing is
	// that a bare threshold is not a function of distance alone once the NPC's own movement is what
	// changes the distance. Closing in the sprint carries it past DuelDistance, the duel stops
	// closing and it drifts back out, and the phase flips on every crossing - many times a second,
	// because the boundary sits exactly where the NPC parks itself.
	//
	// Everything the player reported followed from that one flip: the sprint was entered constantly
	// but never lasted long enough to accelerate (so no sprint, and no slide either, since CanSlide
	// wants SlideMinStartSpeed), every edge dropped the current leg and issued a fresh MoveTo (so
	// the zigzag legs were chopped to fragments and the NPC visibly stuttered), and the rotation
	// mode flipped with it (so the body snapped off the target and back, "sometimes turns away").
	//
	// The fix is to widen whichever band the NPC is already in. Entry thresholds are unchanged, so
	// the phase table in the design doc still reads true; only leaving costs an extra 150 units.
	//
	// The duel is stronger than that: it LATCHES. Author's call, and it changes the shape of the
	// push on purpose - the sprint happens once per push, and the only way out of the duel is the
	// withdrawal, never backwards into another sprint. Distance stops being the whole story here
	// because the duel is the one phase whose own movement is lateral: it does not close, so it
	// drifts, and a purely distance-driven reading would keep re-charging a target it is already
	// standing next to. The cycle still comes back around - the duel clock sets bWithdrawing, the
	// withdrawal runs out to WithdrawDistance, and the next tick resolves to Approach again.
	if (Data.PreviousPhase == EShooterPushPhase::Duel)
	{
		return EShooterPushPhase::Duel;
	}

	const float SprintEdge = (Data.PreviousPhase == EShooterPushPhase::Approach)
		? Data.SprintDistance
		: Data.SprintDistance + ShooterPush_PhaseHysteresis;

	if (Distance > SprintEdge)
	{
		return EShooterPushPhase::Approach;
	}
	if (Distance > Data.DuelDistance)
	{
		return EShooterPushPhase::Sprint;
	}
	return EShooterPushPhase::Duel;
}

FVector FSTTask_ShooterPush::ComputeLegDirection(const FInstanceDataType& Data) const
{
	const FVector ToTarget = (Data.Target->GetActorLocation() - Data.NPC->GetActorLocation()).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero())
	{
		return FVector::ForwardVector;
	}

	// The duel is pure lateral: no closing at all, which is what makes it read as a firing-range
	// dummy rather than as another approach.
	if (Data.Phase == EShooterPushPhase::Duel)
	{
		return FVector::CrossProduct(FVector::UpVector, ToTarget).GetSafeNormal() * Data.LegSign;
	}

	const FVector Base = (Data.Phase == EShooterPushPhase::Withdraw) ? -ToTarget : ToTarget;
	return Base.RotateAngleAxis(Data.LegSign * Data.DiagonalAngleDeg, FVector::UpVector);
}

void FSTTask_ShooterPush::StartLeg(FInstanceDataType& Data) const
{
	if (!Data.NPC || !Data.Controller || !Data.Target)
	{
		return;
	}

	UWorld* const World = Data.NPC->GetWorld();
	UNavigationSystemV1* const NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!World || !NavSys)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	const bool bDuel = Data.Phase == EShooterPushPhase::Duel;
	const bool bSprint = Data.Phase == EShooterPushPhase::Sprint;

	// Leg length follows from how fast this NPC actually moves, so retuning its speed does not
	// silently retune the shape of its path as well.
	const float Speed = FMath::Max(100.0f, Data.NPC->GetCharacterMovement()
		? Data.NPC->GetCharacterMovement()->GetMaxSpeed() : 400.0f);

	float Duration;
	float LegLength;

	// Commit an ANGLE AROUND THE TARGET, once, and keep it for the whole charge. Everything the
	// dynamic behaviour needs follows from storing a bearing instead of a place: while the player
	// walks, the spot rides along with them, the run bends by however much the player moved, and the
	// NPC never re-picks a different corner of the ring and snaps toward it.
	auto CommitSprintBearing = [&Data]()
	{
		FVector FromTarget = (Data.NPC->GetActorLocation() - Data.Target->GetActorLocation()).GetSafeNormal2D();
		if (FromTarget.IsNearlyZero())
		{
			FromTarget = FVector::ForwardVector;
		}

		Data.CommittedBearingDeg = FromTarget
			.RotateAngleAxis(Data.LegSign * Data.DiagonalAngleDeg, FVector::UpVector)
			.Rotation().Yaw;
		Data.bHasCommittedBearing = true;
	};

	FVector SprintDestination = FVector::ZeroVector;

	if (bSprint)
	{
		// The sprint is ONE committed run, not a zigzag: pick the spot where the duel is going to
		// start and go there. Author's call, and it is also what makes the sprint readable - a
		// charge that changes its mind twice on the way is not a charge.
		//
		// The destination is a point ON the duel ring, not a direction and a length. The first
		// attempt at this ran a fixed heading DiagonalAngleDeg off the line to the target, with the
		// length divided by the cosine, and that cannot work: a straight line at angle T to the
		// target never comes closer than D*sin(T). At D=2000 and 35 degrees that floor is 1147, so a
		// charge aimed at a 750 ring stopped dead around 800 and then re-issued a goal a few units
		// ahead of itself, inside the acceptance radius, forever. Measured: two NPCs frozen at 827
		// and 791 with speed 0.
		//
		// Anchoring on the ring instead makes the arrival exact by construction, and the heading
		// comes out of the geometry rather than being imposed on it: still a diagonal, just one that
		// actually lands where the duel starts.
		if (!Data.bHasCommittedBearing)
		{
			CommitSprintBearing();
		}

		SprintDestination = ShooterPush_RingPoint(Data);
		LegLength = FVector::Dist2D(Data.NPC->GetActorLocation(), SprintDestination);

		// Not a pacing knob - a safety net. Without it a charge blocked by something the stuck check
		// does not catch would hold the leg forever. Generous on purpose so it never trims a run
		// that is actually progressing.
		Duration = (LegLength / Speed) * 2.0f + 1.0f;
	}
	else
	{
		Duration = bDuel
			? FMath::FRandRange(Data.StrafeHoldMin, Data.StrafeHoldMax)
			: FMath::FRandRange(Data.LegDurationMin, Data.LegDurationMax);
		LegLength = Speed * Duration;
	}

	const FVector Origin = Data.NPC->GetActorLocation();

	// Two tries: the intended side, then the other one. A diagonal into a wall is the normal case in
	// a corridor, and flipping is both the cheapest answer and the one that looks deliberate.
	FVector Destination = FVector::ZeroVector;
	bool bFound = false;
	for (int32 Attempt = 0; Attempt < 2 && !bFound; ++Attempt)
	{
		// On the retry the sign has already flipped, so a sprint re-commits to the mirrored bearing;
		// any other leg just recomputes its direction.
		if (bSprint && Attempt > 0)
		{
			CommitSprintBearing();
			SprintDestination = ShooterPush_RingPoint(Data);
		}

		const FVector Candidate = bSprint
			? SprintDestination
			: Origin + ComputeLegDirection(Data) * LegLength;

		FNavLocation NavResult;
		if (NavSys->ProjectPointToNavigation(Candidate, NavResult, FVector(200.0f, 200.0f, 300.0f)))
		{
			Destination = NavResult.Location;
			bFound = true;
			break;
		}

		Data.LegSign = -Data.LegSign;
	}

	if (!bFound)
	{
		// Nowhere to go this tick. Keep facing the target and let the next tick try again rather
		// than blindly walking off the mesh.
		Data.bHasLeg = false;
		Data.LegEndTime = Now + 0.25f;
		return;
	}

	FAIMoveRequest MoveRequest;
	MoveRequest.SetGoalLocation(Destination);
	MoveRequest.SetAcceptanceRadius(60.0f);
	MoveRequest.SetUsePathfinding(true);
	MoveRequest.SetAllowPartialPath(true);
	MoveRequest.SetProjectGoalLocation(true);
	MoveRequest.SetCanStrafe(true);

	const FPathFollowingRequestResult Result = Data.Controller->MoveTo(MoveRequest);

	// AlreadyAtGoal is not success, and treating it as one is how the NPC stood still while looking
	// busy: a goal inside the 60cm acceptance radius makes MoveTo a no-op, the tick sees the goal
	// reached, asks for another leg, gets another no-op, and nothing ever moves. Handle it like a
	// finished leg instead - short retry, other side next time - so a degenerate destination costs
	// a quarter of a second rather than the rest of the fight.
	if (Result.Code == EPathFollowingRequestResult::Failed
		|| Result.Code == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		Data.bHasLeg = false;
		Data.LegEndTime = Now + 0.25f;
		Data.LegSign = -Data.LegSign;
		return;
	}

	Data.LegDestination = Destination;
	Data.bHasLeg = true;
	Data.LegEndTime = Now + Duration;
	Data.LastStuckCheckPosition = Origin;
	Data.LastStuckCheckTime = Now;

	// Rolled once per leg, not per tick, so a withdrawing NPC either shoots during this leg or does
	// not, instead of stuttering.
	Data.bWithdrawLegFires = FMath::FRand() < Data.WithdrawFireChance;

	// Next leg goes the other way. This is the zigzag - and the sprint is exactly the phase that
	// must not have one, so it keeps its side. If the run gets re-issued mid-charge (goal reached
	// while still outside DuelDistance, or a projection retry), it carries on along the same
	// diagonal instead of cutting back across itself.
	if (!bSprint)
	{
		Data.LegSign = -Data.LegSign;
	}
}

void FSTTask_ShooterPush::StartShooting(FInstanceDataType& Data) const
{
	if (!Data.NPC || !Data.Target)
	{
		return;
	}

	// Permission stays where it already lives: AShooterNPC asks the coordinator itself, so this does
	// not become a second token system with its own opinion.
	Data.NPC->StartShooting(Data.Target);
	Data.bIsShooting = true;
}

void FSTTask_ShooterPush::StopShooting(FInstanceDataType& Data) const
{
	if (Data.NPC)
	{
		Data.NPC->StopShooting();
	}
	Data.bIsShooting = false;
}

void FSTTask_ShooterPush::ReleaseMovementState(FInstanceDataType& Data) const
{
	StopShooting(Data);

	if (!Data.NPC)
	{
		return;
	}

	// The sprint latch outlives this task if it is not cleared: bSprintKeyHeld is a key state, and
	// nothing else in the NPC's world is ever going to let go of it.
	if (UApexMovementComponent* Apex = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement()))
	{
		Apex->StopSprint();

		// Same reasoning as the sprint latch above: the slide's crouch has no key to release it, so
		// an NPC that exits this task mid-slide would keep the crouched pose into whatever state
		// the tree goes to next.
		if (Apex->IsCrouching())
		{
			Apex->StopCrouching();
		}
	}

	if (Data.Controller)
	{
		Data.Controller->StopMovement();
	}
}

#if WITH_EDITOR
FText FSTTask_ShooterPush::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	if (!Data)
	{
		return FText::FromString(TEXT("Push the target"));
	}

	return FText::FromString(FString::Printf(
		TEXT("Push: fire while closing past %.0f, sprint in to %.0f, then duel for %.0fs"),
		Data->SprintDistance, Data->DuelDistance, Data->DuelDuration));
}
#endif
