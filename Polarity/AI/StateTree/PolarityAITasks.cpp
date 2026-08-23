// PolarityAITasks.cpp

#include "PolarityAITasks.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "../Coordination/AICombatCoordinator.h"
#include "../Components/MeleeRetreatComponent.h"
#include "../Components/CoverFinderComponent.h"
#include "../../EMFVelocityModifier.h"
#include "../../Coop/CoopPlayers.h"
#include "../../Variant_Shooter/AI/ShooterNPC.h"
#include "../../Variant_Shooter/AI/ShooterAIController.h"
#include "../../Variant_Shooter/AI/EnemyCombatProfile.h"
#include "../../Variant_Shooter/AI/FlyingDrone.h"
#include "../../Variant_Shooter/AI/FlyingAIMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "AITypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "../../ApexMovementComponent.h"
#include "../../MovementSettings.h"
#include "EngineUtils.h"

// Its own category, off by default, so the push can be watched without the log being unusable the
// rest of the time. Turn it on in the console with:  log LogShooterPush Verbose
DEFINE_LOG_CATEGORY_STATIC(LogShooterPush, Log, All);

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

	// Classes that do not close distance are kept out of this state by the "Can Push" ENTER
	// CONDITION, not from in here.
	//
	// The first attempt did it from in here, by failing on entry, on the assumption that a failed
	// state makes TrySelectChildrenInOrder move to the next sibling. It does not: failing a task
	// completes the state as failed and hands control to that state's transitions, and selection is
	// never re-run. With no transition matching the failure the tree fell through to Search for
	// Enemy, so a rocketeer roamed to random points with its back to the player and only ever came
	// alive when its shield broke and the Peek transition fired. Enter conditions are the only thing
	// that makes selection SKIP a state.
	const UEnemyCombatProfile* const Profile = Data.NPC->GetCombatProfile();

	// Distances from the class, where it has an opinion. The tree's own numbers stay the default for
	// everybody who does not.
	if (Profile && Profile->bOverridePushTuning)
	{
		Data.SprintDistance = Profile->SprintDistance;
		Data.DuelDistance = Profile->DuelDistance;
		Data.DuelDuration = Profile->DuelDuration;
	}

	UE_LOG(LogTemp, Warning, TEXT("[PUSH_DEBUG] %s ENTER Push%s (target %s)"),
		*GetNameSafe(Data.NPC), Data.bNeverWithdraw ? TEXT(" [LAST STAND]") : TEXT(""),
		*GetNameSafe(Data.Target));

	const float Now = Data.NPC->GetWorld()->GetTimeSeconds();

	Data.Phase = ResolvePhase(Data);

	// Last Stand is a finishing move, not a distance band, and the trigger for it is exactly the
	// condition that makes ResolvePhase hand back Duel (or even Approach) directly: the player is
	// already close, because being cornered at close range is what put the NPC here. A
	// distance-driven pick would skip straight past the phase the desperate charge lives in, so it
	// gets forced here instead - but forced to WHICH phase is a design decision, not a given: the
	// charge-and-orbit slide needs open ground to actually happen in, and a slide that skids into
	// the nearest wall reads as broken, not desperate. HasRoomToOrbit is the author's call on how
	// much room counts (see OrbitOpenRadius/OrbitOpenFraction on this task) - open enough gets the
	// full charge, tight gets a straight run at the player instead. Both fire; only the movement
	// shape differs.
	bool bLastStandOrbit = false;
	if (Data.bNeverWithdraw)
	{
		bLastStandOrbit = HasRoomToOrbit(Data);
		Data.Phase = bLastStandOrbit ? EShooterPushPhase::Sprint : EShooterPushPhase::Approach;

		UE_LOG(LogTemp, Warning, TEXT("[PUSH_DEBUG] %s LAST STAND branch: %s"),
			*GetNameSafe(Data.NPC), bLastStandOrbit ? TEXT("charge+orbit (open)") : TEXT("straight approach (tight)"));
	}

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

	// The phase-edge block in Tick is what normally calls Apex->StartSprint() on the transition
	// INTO Sprint, but forcing the phase here means EnterState hands Tick a state where
	// Phase == PreviousPhase already, so that edge never fires. Do its job by hand for the one
	// case that needs it - the open-ground branch only, since the tight branch was forced to
	// Approach instead and Approach's own ordinary zigzag-and-shoot leg (already issued by
	// StartLeg above) is the whole point of that branch.
	if (bLastStandOrbit)
	{
		if (UApexMovementComponent* const Apex = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement()))
		{
			Apex->StartSprint();

			// Guarantee the charge that follows is slide-eligible from its first frame. CanSlide
			// reads Velocity.Size2D() against SlideMinStartSpeed the instant the ring point comes
			// into SlideLeadTime range, and a last stand starts close enough to the player that
			// ordinary acceleration might not have caught up by then - the charge would arrive at
			// the ring, get refused a slide, and just stop there, which reads as crouching in place
			// rather than the desperate lunge this mode exists for. Snap straight to slide-eligible
			// speed along the charge heading instead of waiting for it to build up.
			if (Apex->MovementSettings)
			{
				const float BoostSpeed = FMath::Max(Apex->GetMaxSpeed(), Apex->MovementSettings->SlideMinStartSpeed + 50.0f);

				FVector ChargeDir = (Data.LegDestination - Data.NPC->GetActorLocation()).GetSafeNormal2D();
				if (ChargeDir.IsNearlyZero())
				{
					ChargeDir = Data.NPC->GetActorForwardVector().GetSafeNormal2D();
				}

				if (!ChargeDir.IsNearlyZero())
				{
					Apex->Velocity = ChargeDir * BoostSpeed + FVector(0.0f, 0.0f, Apex->Velocity.Z);
				}
			}
		}
	}

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
	if (Data.Phase == EShooterPushPhase::Duel && !Data.bWithdrawing && !Data.bNeverWithdraw
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
		UE_LOG(LogTemp, Warning, TEXT("[PUSH_DEBUG] %s%s phase %d -> %d (dist %.0f)"),
			*GetNameSafe(Data.NPC), Data.bNeverWithdraw ? TEXT(" [LAST STAND]") : TEXT(""),
			static_cast<int32>(Data.PreviousPhase), static_cast<int32>(Data.Phase),
			Data.Target ? FVector::Dist2D(Data.Target->GetActorLocation(), Data.NPC->GetActorLocation()) : -1.0f);

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

		// Gated on the class, because "advances without ever breaking into a run" is a silhouette in
		// its own right. This is the PUSH only: the relocation sprint the peek uses is a different
		// call, so a juggernaut still runs when it has to cross open ground under covering fire, and
		// only its advance is slow. The walking speed itself is not touched here - that lives in the
		// MovementSettings asset, which is the one place that decides how fast anything moves.
		const UEnemyCombatProfile* const SprintProfile = Data.NPC->GetCombatProfile();
		const bool bMaySprint = !SprintProfile || SprintProfile->bSprintWhenPushing;

		if (Apex && bMaySprint && Data.Phase == EShooterPushPhase::Sprint)
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
	//
	// Third clause, and it overrules the run animation: an NPC that is SHOOTING during the charge
	// has to be looking at what it shoots. Facing the movement direction is only defensible while
	// the charge is silent, which is the ordinary case (bFireWhileSprinting is off by default) and
	// is why this went unnoticed for so long. Last Stand turns firing on, and the mismatch became
	// immediately visible: measured 2026-08-18, every FACING_DEBUG sample inside a sprint window
	// had Orient2Move=1 with YawToTgt drifting to -57 degrees, i.e. the body - and the gun with it -
	// aimed up to 57 degrees off the player while the shots themselves still went to the player.
	// That is the "fires a burst that does not come out parallel to the barrel".
	const UApexMovementComponent* const ApexRotation = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement());
	const bool bChargingOnFoot = Data.Phase == EShooterPushPhase::Sprint
		&& !Data.bFireWhileSprinting
		&& !(ApexRotation && ApexRotation->IsSliding());

	SetShooterRotationMode(Data.NPC, /*bFaceTarget*/ !bChargingOnFoot);

	// ---- The charge, while it is running ----
	//
	// Three things happen here and nowhere else, because all three depend on where the committed
	// spot is RIGHT NOW rather than where it was when the leg was issued.
	if (Data.Phase == EShooterPushPhase::Sprint && Data.bHasCommittedBearing)
	{
		UApexMovementComponent* const ApexRun = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement());

		// Walk the committed bearing around the target while the slide runs, so the spot it is
		// steering at travels along the ring and the skid comes out as an arc AROUND the player
		// instead of a straight line into them.
		//
		// Only while actually sliding: the run in has to stay a straight, readable charge, and the
		// curve is the thing that happens at the end of it.
		//
		// Advancing the ANGLE and not the point is what keeps this stable. The radius never changes,
		// so the spot cannot drift off into the level, and the movement component rate limits the
		// turn anyway.
		if (Data.SlideOrbitRateDeg > 0.0f && ApexRun && ApexRun->IsSlidingToPoint())
		{
			Data.CommittedBearingDeg = FRotator::NormalizeAxis(
				Data.CommittedBearingDeg + Data.LegSign * Data.SlideOrbitRateDeg * DeltaTime);
		}

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

			UE_LOG(LogShooterPush, Verbose, TEXT("[AI_DEBUG] %s phase=%d dist=%.0f (sprint@%.0f duel@%.0f) ")
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
		// Silent by default: the quiet run is what makes a charge read as a charge. The cornered
		// variant turns this on, and the noise is the point.
		bWantsFire = Data.bFireWhileSprinting;
		break;
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

	// Last Stand's forced entry into Sprint (see EnterState) needs the same kind of latch, for the
	// same reason: it exists specifically for the case where the player is already this close, and
	// the ordinary distance ladder below would read that as "already in Duel range" and hand the
	// phase straight back before the charge covers any ground at all - one frame of sprint key and
	// a velocity snap, then gone. Hold Sprint until the charge actually arrives (bChargeArrived,
	// checked above every tick) the same way a normal charge would arrive from far away.
	if (Data.bNeverWithdraw && Data.PreviousPhase == EShooterPushPhase::Sprint)
	{
		return EShooterPushPhase::Sprint;
	}

	// The other half of Last Stand's forced entry (see EnterState): when HasRoomToOrbit said no,
	// the forced phase is Approach instead of Sprint, for the same distance-ladder reason - close
	// range would otherwise read straight back as Duel and the "run at the player and shoot" this
	// is meant to be would never get further than a single frame. Author's call that this one has
	// no arrival condition of its own: unlike the charge, a tight-corner Last Stand does not have a
	// separate duel phase to arrive INTO, it simply keeps closing and firing for as long as Last
	// Stand lasts.
	if (Data.bNeverWithdraw && Data.PreviousPhase == EShooterPushPhase::Approach)
	{
		return EShooterPushPhase::Approach;
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

bool FSTTask_ShooterPush::HasRoomToOrbit(const FInstanceDataType& Data) const
{
	if (Data.OrbitOpenRadius <= 0.0f || !Data.NPC || !Data.Target)
	{
		return false;
	}

	UWorld* const World = Data.NPC->GetWorld();
	if (!World)
	{
		return false;
	}

	// Around the TARGET, not around the NPC: the orbit happens near the player, so a corner that is
	// tight next to the player is what refuses this, regardless of how open the NPC's own starting
	// spot happens to be.
	const FVector Center = Data.Target->GetActorLocation();

	FCollisionQueryParams TraceParams(FName(TEXT("LastStandOrbitRoom")), /*bTraceComplex*/ false);
	TraceParams.AddIgnoredActor(Data.NPC);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		// A body standing in the way is not a wall. This is checking whether the ARENA has room for
		// the maneuver, not whether it is currently unoccupied.
		TraceParams.AddIgnoredActor(*It);
	}

	constexpr int32 NumSamples = 8;
	int32 ClearCount = 0;

	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const float AngleDeg = (360.0f / NumSamples) * Index;
		const FVector Direction = FRotator(0.0f, AngleDeg, 0.0f).Vector();
		const FVector TraceEnd = Center + Direction * Data.OrbitOpenRadius;

		if (!World->LineTraceTestByChannel(Center, TraceEnd, ECC_Visibility, TraceParams))
		{
			++ClearCount;
		}
	}

	return (static_cast<float>(ClearCount) / static_cast<float>(NumSamples)) >= Data.OrbitOpenFraction;
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
	auto CommitSprintBearing = [&Data](bool bIgnoreCoordinator)
	{
		// First choice: the angle the coordinator already assigned this NPC in the battle circle.
		//
		// This is design doc 4.4 - "the pusher keeps its slot angle and ignores only the radius" -
		// and it is the whole answer to enemies piling into one silhouette. Without it every pusher
		// derives its bearing from its own position, and since they are all converging on the same
		// player they all converge on the same line, block each other's shot and end up shoulder to
		// shoulder. The coordinator hands out spread angles already; taking the ANGLE from the slot
		// and the RADIUS from DuelDistance means they close from different sides for free, and the
		// ring keeps working for everybody who is not pushing.
		if (!bIgnoreCoordinator)
		{
			if (const AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC))
			{
				FVector SlotPosition = FVector::ZeroVector;
				if (Coordinator->GetAssignedSlotPosition(Data.NPC, SlotPosition))
				{
					const FVector TargetToSlot =
						(SlotPosition - Data.Target->GetActorLocation()).GetSafeNormal2D();

					if (!TargetToSlot.IsNearlyZero())
					{
						Data.CommittedBearingDeg = TargetToSlot.Rotation().Yaw;
						Data.bHasCommittedBearing = true;
						return;
					}
				}
			}
		}

		// Fallback: the diagonal off its own approach line. Used when the battle circle is switched
		// off, when this NPC holds no slot, and on the projection retry, where the point has to move
		// to the other side and the slot angle would keep handing back the same one.
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
			CommitSprintBearing(/*bIgnoreCoordinator*/ false);
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
		// any other leg just recomputes its direction. The coordinator is skipped here on purpose:
		// its slot angle does not depend on the sign, so asking again would hand back the exact
		// point that just failed to project.
		if (bSprint && Attempt > 0)
		{
			CommitSprintBearing(/*bIgnoreCoordinator*/ true);
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

// ============================================================================
// Shield state conditions
// ============================================================================

namespace
{
	/** The one place these conditions decide what "shield" means, so the two of them cannot drift
	 *  apart. Mirrors AShooterWeapon::IsTargetShieldDown for the enemy case: the charge meter lives
	 *  on UEMFVelocityModifier and IsAtMaxCharge already compares the MAGNITUDE against that
	 *  component's own ceiling, which is what makes either polarity count as broken.
	 *
	 *  bOutHasShield says whether the question applied at all. An NPC with no charge component has
	 *  no shield to lose, and the callers below deliberately answer "not down" for it rather than
	 *  inheriting the weapon's "freely hurtable" reading. */
	bool IsShooterShieldDown(const AShooterNPC* NPC, bool& bOutHasShield)
	{
		bOutHasShield = false;

		if (!IsValid(NPC))
		{
			return false;
		}

		const UEMFVelocityModifier* const Modifier = NPC->FindComponentByClass<UEMFVelocityModifier>();
		if (!Modifier)
		{
			return false;
		}

		bOutHasShield = true;
		return Modifier->IsAtMaxCharge();
	}
}

bool FSTCondition_ShooterShieldDown::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	bool bHasShield = false;
	return IsShooterShieldDown(Data.NPC, bHasShield) && bHasShield;
}

bool FSTCondition_ClassCanPush::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!IsValid(Data.NPC))
	{
		return true;
	}

	// No profile means no opinion, and no opinion means yes: every enemy in the game predates
	// profiles and must keep behaving exactly as it did.
	const UEnemyCombatProfile* const Profile = Data.NPC->GetCombatProfile();
	return !Profile || Profile->bCanPush;
}

bool FSTCondition_ShooterShieldUp::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	// No charge component means the mechanic never applied, and such an NPC must keep pushing, so it
	// reads as shielded here. Same reasoning as the comment on the pair in the header.
	if (!IsValid(Data.NPC))
	{
		return true;
	}

	const UEMFVelocityModifier* const Modifier = Data.NPC->FindComponentByClass<UEMFVelocityModifier>();
	if (!Modifier)
	{
		return true;
	}

	const float Cap = Modifier->MaxBaseCharge;
	if (Cap <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	// Deliberately NOT !IsAtMaxCharge(): see the header. The ceiling is where the shield BREAKS; it
	// counts as restored only after the recovery curve has taken the charge back down to a fraction
	// of that ceiling. Magnitude, so either polarity reads the same way, exactly as IsAtMaxCharge does.
	const float Charge = FMath::Abs(Modifier->GetCharge());
	const float Threshold = Cap * RecoveredFraction;
	const bool bUp = Charge <= Threshold;

	// Deliberately NOT logged. A transition condition is evaluated every tick of every NPC, so a log
	// here produced tens of lines per second per enemy and buried every other diagnostic in the
	// file - which is exactly what it did while the grenadier bug was being chased. If this needs
	// measuring again, log it on the EDGE from the task that acts on it, not from the predicate.
	return bUp;
}

#if WITH_EDITOR
FText FSTCondition_ShooterShieldDown::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("NPC charge is at its ceiling (shield down)"));
}

FText FSTCondition_ClassCanPush::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Class profile allows closing distance"));
}

FText FSTCondition_ShooterShieldUp::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("NPC still has shield (charge below ceiling)"));
}
#endif

// ============================================================================
// ShooterPeek
// ============================================================================

namespace
{
	/** Только для логов [PEEK_DEBUG]. EShooterPeekPhase не UENUM, рефлексии у него нет. */
	const TCHAR* PeekPhaseName(EShooterPeekPhase Phase)
	{
		switch (Phase)
		{
		case EShooterPeekPhase::Seeking: return TEXT("Seeking");
		case EShooterPeekPhase::ToHide:  return TEXT("ToHide");
		case EShooterPeekPhase::AtHide:  return TEXT("AtHide");
		case EShooterPeekPhase::ToPeek:  return TEXT("ToPeek");
		case EShooterPeekPhase::AtPeek:  return TEXT("AtPeek");
		default:                         return TEXT("?");
		}
	}
}

/** Multiple of ArriveRadius that still counts as having reached a cover point, once path following
 *  has stopped and the NPC is no longer closing.
 *
 *  This exists because the ENGINE and this task disagreed about the word "arrived", and the engine
 *  is the one actually driving the pawn. UPathFollowingComponent::HasReachedInternal tests
 *
 *      AcceptanceRadius + GoalRadius + AgentRadius * MinAgentRadiusPct
 *
 *  and FAIMoveRequest sets bReachTestIncludesAgentRadius true by default, with
 *  MinAgentRadiusPct = 1.1. For this NPC that is 60 + 0 + 34 * 1.1 = 97.4 units
 *  (PolarityCharacter.cpp sets the capsule radius to 34). So path following declares the move
 *  finished and stops driving the pawn anywhere inside ~97 units, while this task went on comparing
 *  the raw distance against a bare 60 that could then never be reached.
 *
 *  Measured 2026-08-18 across a whole session, every abandoned walk gave up at 88, 90, 91, 93, 95,
 *  96 or 97 units - every one of them under that 97.4 ceiling, on a dozen different corners. The
 *  NPC was not stuck on geometry and was not short of room; it had been told it was there.
 *
 *  Derived from that same formula rather than guessed as a multiple of ArriveRadius, and the
 *  difference is not cosmetic. A flat multiplier has to be generous enough for the worst agent,
 *  which made the tolerance 120 units while PeekStepDistance is 150 - so "walk from H to P" was
 *  satisfied after 30 units of travel. That is the micro-step: a step out barely longer than the
 *  tolerance for having finished it, two seconds of standing, and 30 units back. The header comment
 *  on ArriveRadius warns about exactly this ("well under that or the two ends collapse into one").
 *  Reading the real capsule keeps the tolerance at the engine's own ~97 and leaves the step intact. */
static float ShooterPeek_EffectiveArriveRadius(const FSTTask_ShooterPeek_Data& Data)
{
	// MinAgentRadiusPct, the engine's own constant in PathFollowingComponent.cpp. The small margin
	// on top is so this test passes on the tick path following declares the move done, rather than
	// landing exactly on the boundary and depending on float luck.
	constexpr float MinAgentRadiusPct = 1.1f;
	constexpr float Margin = 5.0f;

	const float AgentRadius = Data.NPC ? Data.NPC->GetSimpleCollisionRadius() : 0.0f;
	return Data.ArriveRadius + AgentRadius * MinAgentRadiusPct + Margin;
}

EStateTreeRunStatus FSTTask_ShooterPeek::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC || !Data.Controller || !Data.Target)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Rhythm from the class, where it has an opinion. Same arrangement as the push: the tree's
	// numbers are the default and a profile may replace them, so one shared tree keeps serving every
	// class without any of them having to agree about pacing.
	if (const UEnemyCombatProfile* const Profile = Data.NPC->GetCombatProfile())
	{
		if (Profile->bOverridePeekTuning)
		{
			Data.HideDuration = Profile->HideDuration;
			Data.PeekDuration = Profile->PeekDuration;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[PEEK_DEBUG] %s ENTER Peek (target %s)"),
		*GetNameSafe(Data.NPC), *GetNameSafe(Data.Target));

	Data.Phase = EShooterPeekPhase::Seeking;
	Data.PhaseElapsed = 0.0f;
	Data.SinceRecheck = 0.0f;
	Data.SinceOpportunisticSearch = 0.0f;
	Data.FailedSearches = 0;
	Data.bWasSearching = false;
	Data.LegElapsed = 0.0f;
	Data.LegDuration = 0.0f;
	Data.bIsShooting = false;
	Data.bPeekedSinceCover = false;
	Data.BestGoalDistance = TNumericLimits<float>::Max();
	Data.SinceGoalProgress = 0.0f;
	Data.bIsRelocating = false;
	Data.SuppressionTarget = nullptr;
	Data.CornerElapsed = 0.0f;
	Data.LastSeenBurstShots = Data.NPC->GetCurrentBurstShots();
	Data.bFiredFromCorner = false;
	Data.bHasLastSeen = false;

	// Which way the first leg goes is a coin flip, so two enemies peeking from the same corner do
	// not step out in lockstep.
	Data.LegSign = FMath::RandBool() ? 1.0f : -1.0f;

	// The first request usually refuses: UCoverFinderComponent starts with LastQueryTime at zero, so
	// the cooldown has not elapsed yet in the opening seconds of a level. Not a failure - Seeking
	// simply asks again next tick.
	if (UCoverFinderComponent* const Finder = Data.NPC->FindComponentByClass<UCoverFinderComponent>())
	{
		Finder->RequestCover(Data.Target);
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_ShooterPeek::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!IsValid(Data.NPC) || !IsValid(Data.Controller) || !IsValid(Data.Target))
	{
		return EStateTreeRunStatus::Failed;
	}

	UCoverFinderComponent* const Finder = Data.NPC->FindComponentByClass<UCoverFinderComponent>();
	if (!Finder)
	{
		// Nothing to peek from and no way to get one. Failing hands the NPC back to the tree, which
		// still has Push and the search states, rather than leaving it standing.
		return EStateTreeRunStatus::Failed;
	}

	Data.PhaseElapsed += DeltaTime;
	Data.CornerElapsed += DeltaTime;

	// A shot LANDING, caught on the rising edge of the burst counter rather than read as a level.
	// The level would be true for the whole rest of the burst and for every tick after it, so a
	// class that leaves after firing would leave again the instant it arrived somewhere new.
	{
		const int32 BurstShots = Data.NPC->GetCurrentBurstShots();
		if (BurstShots > Data.LastSeenBurstShots)
		{
			Data.bFiredFromCorner = true;
		}
		Data.LastSeenBurstShots = BurstShots;
	}

	// Facing is constant across every phase: the corner changes where it stands, not who it watches.
	//
	// BOTH halves of that, and the second one used to be missing entirely. SetFocus only aims the
	// CONTROLLER; whether the body follows the controller or follows its own movement direction is
	// UCharacterMovementComponent's rotation mode, and this task never touched it - so it inherited
	// whatever ShooterPush happened to leave behind. Push sets orient-to-movement for the charge
	// (see bChargingOnFoot), so a shield that broke MID-SPRINT handed Peek a body steering itself by
	// its footsteps, and the focus was quietly ignored for the whole peek: it hid, stepped out and
	// fired with the gun pointing wherever it last walked, i.e. into a wall. Breaking the shield in
	// any other phase left the mode already correct, which is exactly why this looked intermittent.
	//
	// Set every tick rather than once on entry, for the same drift reason Push does it that way.
	//
	// The run is the one exception, and it is the same exception ShooterPush makes for its charge:
	// an NPC sprinting to a new corner faces where it is going, because that is the action the run
	// animation describes and because it is not shooting anyway. EndRelocationRun puts it back.
	if (!Data.bIsRelocating)
	{
		Data.Controller->SetFocus(Data.Target);
		SetShooterRotationMode(Data.NPC, /*bFaceTarget*/ true);
	}

	// Whose fight this NPC is in right now: its own, or a teammate's run. Asked before anything
	// else reads Target, because suppression replaces who it shoots at for the duration.
	const bool bSuppressing = UpdateSuppressionDuty(Data);
	AActor* const FireTarget = bSuppressing ? static_cast<AActor*>(Data.SuppressionTarget) : Data.Target.Get();

	if (bSuppressing)
	{
		Data.Controller->SetFocus(Data.SuppressionTarget);
	}

	// A search that just finished. Counted on the falling edge and nowhere else, so a request the
	// component refused on its cooldown never looks like a failure to find anything.
	const bool bSearchingNow = Finder->IsSearching();
	if (Data.bWasSearching && !bSearchingNow)
	{
		if (Finder->HasCover())
		{
			Data.FailedSearches = 0;
		}
		else
		{
			++Data.FailedSearches;
		}
	}
	Data.bWasSearching = bSearchingNow;

	// ---- Cornered ----
	//
	// Two ways out of the peek and into the charge, and both mean the same thing: there is no
	// hiding left to do. Succeeded is the signal; the tree owns what happens next.
	if (Data.FailedSearches >= Data.LastStandFailedSearches)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PEEK_DEBUG] %s -> LAST STAND: %d failed searches"),
			*GetNameSafe(Data.NPC), Data.FailedSearches);
		return EStateTreeRunStatus::Succeeded;
	}

	if (Data.LastStandPlayerDistance > 0.0f)
	{
		// Nearest PLAYER, not the current target: somebody flanking the corner has pushed it just as
		// much as the one being shot at, and in coop that is usually a different person.
		if (const APawn* const Nearest = CoopPlayers::GetNearest(Data.NPC->GetWorld(), Data.NPC->GetActorLocation()))
		{
			const float PlayerDist = FVector::Dist2D(Nearest->GetActorLocation(), Data.NPC->GetActorLocation());
			if (PlayerDist <= Data.LastStandPlayerDistance)
			{
				UE_LOG(LogTemp, Warning, TEXT("[PEEK_DEBUG] %s -> LAST STAND: player %s at %.0f (limit %.0f)"),
					*GetNameSafe(Data.NPC), *GetNameSafe(Nearest), PlayerDist, Data.LastStandPlayerDistance);
				return EStateTreeRunStatus::Succeeded;
			}
		}
	}

	if (!Finder->HasCover())
	{
		// Lost the spot, or never had one. Stop firing, stop moving to a stale destination, and ask
		// again as soon as the component's own cooldown allows.
		if (Data.Phase != EShooterPeekPhase::Seeking)
		{
			StopShooting(Data);
			Data.Controller->StopMovement();
			EnterPhase(Data, EShooterPeekPhase::Seeking);
		}

		if (!Finder->IsSearching() && Finder->GetRequeryCooldownRemaining() <= 0.0f)
		{
			Finder->RequestCover(Data.Target);
		}

		return EStateTreeRunStatus::Running;
	}

	const FCoverSpot& Spot = Finder->GetCover();

	// A fresh spot arrived while Seeking: start walking to its hide end.
	if (Data.Phase == EShooterPeekPhase::Seeking)
	{
		// A NEW spot, so this NPC owes it one peek before the recheck is allowed to give up on it.
		Data.bPeekedSinceCover = false;

		// New corner, new clock and a clean sheet on having fired from it. Both are per POSITION,
		// which is what makes "never shoot twice from the same place" mean anything.
		Data.CornerElapsed = 0.0f;
		Data.bFiredFromCorner = false;

		EnterPhase(Data, EShooterPeekPhase::ToHide);
		TryMoveTo(Data, Spot.HideLocation);
		return EStateTreeRunStatus::Running;
	}

	const FVector NPCLocation = Data.NPC->GetActorLocation();

	switch (Data.Phase)
	{
	case EShooterPeekPhase::ToHide:
	{
		const float ToHideDistance = FVector::Dist2D(NPCLocation, Spot.HideLocation);

		// Arrived, or as arrived as a capsule can get against the wall this corner is made of. The
		// second clause is not a tolerance for sloppiness: without it the NPC parks ~90 units out
		// with nothing left to walk toward, and the ONLY thing that ever moved it on was the stall
		// timeout, which also threw the corner away and started the whole search over. That loop is
		// what "they just stand there behind cover" was.
		// One test, not two. Requiring path following to ALSO report Idle looked like a safety net
		// and was actually the lock: measured 2026-08-18 at 19:30, "STALLED stepping out to P, 90
		// left (best 97)" with an effective radius of 102 - the distance clause passed and the
		// status clause did not, so the NPC stood in the open for the full 5.2s timeout on every
		// other cycle. Inside the effective radius the engine will not drive the pawn any closer no
		// matter what its status field says, so the distance alone is the honest answer.
		// A long walk is a RUN, announced to the squad so somebody covers it. Asked once per walk
		// and only above the threshold: a shuffle behind the same wall does not deserve two
		// teammates standing in the open on its behalf. A refusal means the squad already has as
		// many runners as it can cover, and this NPC simply walks it instead.
		if (!Data.bIsRelocating && ToHideDistance > Data.RelocationSprintDistance)
		{
			BeginRelocationRun(Data, ToHideDistance);
		}

		if (ToHideDistance <= ShooterPeek_EffectiveArriveRadius(Data))
		{
			EndRelocationRun(Data);
			Data.Controller->StopMovement();
			EnterPhase(Data, EShooterPeekPhase::AtHide);
		}
		else if (IsMoveStalled(Data, DeltaTime, ToHideDistance))
		{
			// Genuinely stuck, not merely far. Drop the claim so the corner is not held by an NPC
			// that cannot reach it, and go find another one.
			UE_LOG(LogTemp, Warning, TEXT("[PEEK_DEBUG] %s STALLED walking to H, %.0f left (best %.0f, arrive<=%.0f)"),
				*GetNameSafe(Data.NPC), ToHideDistance, Data.BestGoalDistance, ShooterPeek_EffectiveArriveRadius(Data));

			Finder->ReleaseCover();
			Data.Controller->StopMovement();
			EnterPhase(Data, EShooterPeekPhase::Seeking);
		}
		break;
	}

	case EShooterPeekPhase::AtHide:
	{
		// Players move, so a good H stops being one with nothing happening to the NPC. Cheap check,
		// but not every tick: one trace per living player.
		// Shoot and scoot. A class built this way abandons the corner once it has actually landed a
		// shot from it, rather than settling into the hide/peek rhythm - so the position it fired
		// from is never the position it fires from next, and learning where it was teaches nothing
		// about where it is.
		//
		// Two guards, and both are load-bearing. It waits for a real shot (the burst-counter edge
		// above) so a corner it never managed to fire from is not thrown away as if it had worked.
		// And it waits out MinCornerSeconds, because an arena with few corners would otherwise turn
		// this into a metronome bouncing between the same two spots every time the weapon comes up.
		if (const UEnemyCombatProfile* const ScootProfile = Data.NPC->GetCombatProfile())
		{
			if (ScootProfile->bOverridePeekTuning && ScootProfile->bRelocateAfterFiring
				&& Data.bFiredFromCorner && Data.CornerElapsed >= ScootProfile->MinCornerSeconds)
			{
				UE_LOG(LogTemp, Warning, TEXT("[PEEK_DEBUG] %s fired from this corner, moving on (held %.1fs)"),
					*GetNameSafe(Data.NPC), Data.CornerElapsed);

				Finder->ReleaseCover();
				EnterPhase(Data, EShooterPeekPhase::Seeking);
				break;
			}
		}

		// Not before the first peek. The recheck clock is shorter than HideDuration, so on an open
		// arena it always reached its verdict first, condemned the spot, and sent the NPC back to
		// the search: it hid, never stepped out, and did that forever. One trade per spot is the
		// floor, and only after it has been paid may exposure retire the corner.
		Data.SinceRecheck += DeltaTime;
		if (Data.bPeekedSinceCover && Data.SinceRecheck >= Data.CoverRecheckInterval)
		{
			Data.SinceRecheck = 0.0f;

			if (!Finder->IsCoverStillGood())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[PEEK_DEBUG] %s DROPPED cover at AtHide: exposure %.2f over threshold, sat %.2fs of %.2fs"),
					*GetNameSafe(Data.NPC), Finder->EvaluateCurrentExposure(), Data.PhaseElapsed, Data.HideDuration);

				Finder->ReleaseCover();
				EnterPhase(Data, EShooterPeekPhase::Seeking);
				break;
			}
		}

		// "Has anything better opened up", on its own slower clock - but ONLY once the current corner
		// has stopped working.
		//
		// Unconditionally, this was churn with a nice name. The premise was that the component
		// returns the lowest exposure it can reach, so a result is always at least as good; what it
		// actually returns is the lowest exposure FROM WHERE THE NPC IS STANDING RIGHT NOW, and the
		// player keeps moving, so the "best" corner keeps changing and the NPC keeps chasing it. In
		// game that is an enemy abandoning perfectly good cover for no visible reason and walking
		// back and forth across open ground between two corners, which is exactly what it looked
		// like.
		//
		// Gated on the recheck instead: while this corner still hides it, there is nothing to
		// improve and the NPC stays put. IsCoverStillGood is the same test the recheck above uses,
		// so the two cannot disagree about whether the spot is finished.
		Data.SinceOpportunisticSearch += DeltaTime;
		if (Data.SinceOpportunisticSearch >= Data.OpportunisticSearchInterval)
		{
			Data.SinceOpportunisticSearch = 0.0f;

			if (!Finder->IsSearching() && Finder->GetRequeryCooldownRemaining() <= 0.0f
				&& !Finder->IsCoverStillGood())
			{
				Finder->RequestCover(Data.Target);
			}
		}

		// The corner moved out from under it, which is what a better one arriving looks like from
		// here. No bookkeeping needed to detect it: standing at H and H being somewhere else is the
		// same test either way, and it also covers the spot being replaced for any other reason.
		//
		// Measured against the SAME widened radius the arrival above uses, and it has to be: an NPC
		// that arrived on the capsule-versus-wall fallback stands ~90 units from H by definition, so
		// testing this at the bare 60 would read its own legitimate parking spot as "the corner
		// moved" and bounce it straight back into ToHide, every tick, forever.
		if (FVector::Dist2D(NPCLocation, Spot.HideLocation) > ShooterPeek_EffectiveArriveRadius(Data))
		{
			// A different corner, so its one owed peek starts over with it.
			Data.bPeekedSinceCover = false;

			EnterPhase(Data, EShooterPeekPhase::ToHide);
			TryMoveTo(Data, Spot.HideLocation);
			break;
		}

		// A peek that steps out with an unusable weapon is a peek wasted, and it is the single
		// biggest source of "it went out and just stood there": the whole exposed part of the cycle
		// is about half a second, while the burst cooldown after a full burst is 1.5s and a reload
		// is longer still, so any peek whose window lands inside one is silent by construction.
		//
		// Waiting behind cover instead is both the correct behaviour and the free one - it is
		// already hidden, so the extra time costs nothing and reads as reloading behind the corner
		// rather than as hesitation in the open. HideDuration therefore sets the MINIMUM hide, not
		// the exact one.
		const bool bWeaponReady = !Data.NPC->IsInBurstCooldown() && !Data.NPC->IsReloadingWeapon();

		if (Data.PhaseElapsed >= Data.HideDuration && bWeaponReady)
		{
			EnterPhase(Data, EShooterPeekPhase::ToPeek);

			// The step out is the one move whose refusal used to cost the whole peek. Ignoring the
			// result meant a failed order looked exactly like a slow walk: the NPC stood at H with
			// no path issued, waiting out the full stall timeout before anything happened at all.
			// Measured 2026-08-18, "STALLED stepping out to P, 139 left (best 150)" - a best of 150
			// is the untouched H-to-P distance, i.e. it never took a single step. Fail straight back
			// to the hide instead, which re-arms the peek on the next HideDuration rather than
			// burning MoveTimeout seconds standing in the open doing nothing.
			// Dropping the corner rather than retrying it: a step-out that cannot be pathed is a
			// property of THIS corner, so sitting on it and re-attempting every HideDuration is a
			// slower version of the same standing still. Releasing sends it to Seeking for a
			// different angle, and also lets the failed-search counter eventually reach Last Stand
			// if every angle is refusing, which is the correct answer to "there is nowhere to peek
			// from any more".
			if (!TryMoveTo(Data, Spot.PeekLocation))
			{
				UE_LOG(LogTemp, Warning, TEXT("[PEEK_DEBUG] %s step-out REFUSED, dropping corner"),
					*GetNameSafe(Data.NPC));

				Finder->ReleaseCover();
				Data.Controller->StopMovement();
				EnterPhase(Data, EShooterPeekPhase::Seeking);
			}
		}
		break;
	}

	case EShooterPeekPhase::ToPeek:
	{
		const float ToPeekDistance = FVector::Dist2D(NPCLocation, Spot.PeekLocation);

		// Same fallback as ToHide, and needed for the same reason: P is a step out from a wall, so
		// it is subject to the identical capsule-versus-geometry gap. Measured in the same session,
		// the abandoned step-outs sat at 90 and 95 units.
		// Same single test as ToHide above, and this is the phase the measurement came from.
		if (ToPeekDistance <= ShooterPeek_EffectiveArriveRadius(Data))
		{
			// No StopMovement here on purpose. The strafe leg below replaces the move order
			// outright, and braking first would put a visible plant between stepping out and
			// opening up - the exact stop-and-shoot beat this task exists to avoid.
			EnterPhase(Data, EShooterPeekPhase::AtPeek);

			// This spot has now been traded from, so the exposure recheck is free to condemn it.
			Data.bPeekedSinceCover = true;

			// Fire is NOT started here any more - it has been running since the step out began, and
			// the unified block after this switch owns it. See there.
			StartStrafeLeg(Data, Spot.PeekLocation);
		}
		else if (IsMoveStalled(Data, DeltaTime, ToPeekDistance))
		{
			// Could not step out. Back behind cover rather than standing in the open half way.
			UE_LOG(LogTemp, Warning, TEXT("[PEEK_DEBUG] %s STALLED stepping out to P, %.0f left (best %.0f, arrive<=%.0f)"),
				*GetNameSafe(Data.NPC), ToPeekDistance, Data.BestGoalDistance, ShooterPeek_EffectiveArriveRadius(Data));

			EnterPhase(Data, EShooterPeekPhase::ToHide);
			TryMoveTo(Data, Spot.HideLocation);
		}
		break;
	}

	case EShooterPeekPhase::AtPeek:
	{
		// Standing ON the peek point and still unable to see the target means this corner has
		// stopped being an angle, whatever the hide end of it is doing.
		//
		// Nothing else notices that. IsCoverStillGood only asks whether H is HIDDEN, so a player
		// who walks around the flank leaves the corner scoring perfectly - it hides better than
		// ever - while the peek point it is paired with now stares at a wall. The NPC then cycles
		// hide, step out, see nothing, step back, forever, and because the cover still reads as
		// good the opportunistic search is gated off and never looks for a better angle. That is
		// the "move far enough to the side and it stops peeking and never relocates".
		//
		// Checked here rather than during the walk out: in transit the corner is still between the
		// two of them, so a lost line means nothing until the NPC is actually out at P.
		// Measured against whoever this NPC is actually shooting at, which while suppressing is the
		// teammate's opener rather than its own target. A corner that cannot see the player it has
		// been told to pin is just as useless as one that cannot see its own.
		if (!FireTarget || !Data.NPC->HasLineOfSightTo(FireTarget))
		{
			UE_LOG(LogTemp, Warning, TEXT("[PEEK_DEBUG] %s corner is BLIND from P, releasing"),
				*GetNameSafe(Data.NPC));

			Finder->ReleaseCover();
			Data.Controller->StopMovement();
			EnterPhase(Data, EShooterPeekPhase::Seeking);
			break;
		}

		// Covering fire HOLDS the angle. The ordinary rhythm ducks back every PeekDuration, which is
		// exactly the wrong thing while a teammate is crossing open ground: the point of this duty
		// is that the pressure does not let up until the run is over. The coordinator's own ceiling
		// on suppression time is what stops this being an open-ended invitation to stand and die.
		//
		// It still strafes, through the leg block below. A stationary suppressor is a free kill, and
		// the leash keeps the strafe from turning the hold into a relocation of its own.
		if (bSuppressing)
		{
			Data.LegElapsed += DeltaTime;
			if (Data.LegElapsed >= Data.LegDuration)
			{
				Data.LegSign = -Data.LegSign;
				StartStrafeLeg(Data, Spot.PeekLocation);
			}
			break;
		}

		if (Data.PhaseElapsed >= Data.PeekDuration)
		{
			// Fire deliberately NOT stopped here. The walk back to cover is still exposed, so it is
			// still a shooting phase; the unified block after this switch keeps it running and
			// stops it on reaching AtHide.
			EnterPhase(Data, EShooterPeekPhase::ToHide);
			TryMoveTo(Data, Spot.HideLocation);
			break;
		}

		// The leg boundary is the direction change, and fire is deliberately NOT touched here: the
		// burst carries on across it, so the flip lands in the middle of being shot at rather than
		// between two bursts.
		Data.LegElapsed += DeltaTime;
		if (Data.LegElapsed >= Data.LegDuration)
		{
			Data.LegSign = -Data.LegSign;
			StartStrafeLeg(Data, Spot.PeekLocation);
		}
		break;
	}

	default:
		break;
	}

	// ---- Fire ----
	//
	// One block for the whole task rather than a StartShooting at the moment of arrival, because
	// firing is a property of BEING EXPOSED, not of standing on the peek point. The exposed part of
	// the cycle is the walk out, the moment at P, and the walk back: the NPC is in the open for all
	// three, so it shoots through all three. Only the hide is silent.
	//
	// This is what turns the peek from "run out, plant, empty a magazine, run back" into a firing
	// round trip. Standing still in the open to shoot was the exact stop-and-shoot beat ShooterPush
	// was written to get away from, and doing it at P was the same mistake wearing a different hat.
	//
	// ToHide is conditional because it serves two different journeys: the approach to a FRESH corner
	// (silent - the trade has not started, and the NPC may be crossing open ground to get there) and
	// the retreat after a peek. bPeekedSinceCover already distinguishes them exactly, since it is
	// set on reaching P and cleared whenever a new corner is adopted.
	//
	// Line of sight is re-tested every tick: a corner can take the target out of view mid-strafe,
	// and without this the NPC stays flagged as shooting while the weapon quietly fires at nothing.
	bool bWantsFire = false;
	switch (Data.Phase)
	{
	case EShooterPeekPhase::ToPeek:
	case EShooterPeekPhase::AtPeek:
		bWantsFire = true;
		break;

	case EShooterPeekPhase::ToHide:
		// Silent on a RUN even though it is exposed: the runner is sprinting with its body on its
		// own direction of travel, so it has nothing to aim with. That silence is precisely what the
		// covering fire is buying.
		bWantsFire = Data.bPeekedSinceCover && !Data.bIsRelocating;
		break;

	default:
		// Seeking has no corner yet, AtHide is the whole point of having one.
		bWantsFire = false;
		break;
	}

	const UEnemyCombatProfile* const FireProfile = Data.NPC->GetCombatProfile();

	// Remembered whenever it is true, so the blind-fire branch below has somewhere to aim. Kept even
	// after the line is lost, which is the entire value of it.
	const bool bSeesFireTarget = FireTarget && Data.NPC->HasLineOfSightTo(FireTarget);
	if (bSeesFireTarget)
	{
		Data.LastSeenTargetLocation = FireTarget->GetActorLocation();
		Data.bHasLastSeen = true;
	}

	// Explosives will not fire into their own blast radius. A fire inhibitor and nothing more: the
	// NPC keeps moving, keeps taking cover and keeps relocating while it holds fire, which reads as
	// an enemy giving ground to a weapon it cannot use at this range rather than as one that froze.
	if (bWantsFire && FireProfile && FireProfile->MinFireDistance > 0.0f && FireTarget)
	{
		const float ToFireTarget = FVector::Dist2D(FireTarget->GetActorLocation(), Data.NPC->GetActorLocation());
		if (ToFireTarget < FireProfile->MinFireDistance)
		{
			bWantsFire = false;
		}
	}

	// Blind fire at the last place it saw them. Only for the classes built around it, and only when
	// there is genuinely no line right now: this is the answer to a player who peeks twice from the
	// same corner, not a licence to shoot walls. Aim is through the controller's focus, so the shot
	// leaves in the remembered direction rather than at whatever the NPC happens to be facing.
	// AtPeek ONLY, and that qualifier is the whole fix. "Fire when you cannot see them" is true for
	// the walk out as well, and during that walk the NPC is still behind the corner it is stepping
	// around - so it opened up into its own cover, every cycle, before it had gone anywhere.
	//
	// Standing at P is the one moment where no line to the target means the target has moved rather
	// than that there is a wall in the way, which is exactly the situation this was written for.
	const bool bBlindFire = bWantsFire && !bSeesFireTarget && Data.bHasLastSeen
		&& Data.Phase == EShooterPeekPhase::AtPeek
		&& FireProfile && FireProfile->bFireAtLastSeenWhenBlind;

	if (bBlindFire)
	{
		Data.Controller->SetFocalPoint(Data.LastSeenTargetLocation);
	}

	if (bWantsFire && FireTarget && (bSeesFireTarget || bBlindFire))
	{
		if (!Data.bIsShooting)
		{
			StartShooting(Data, FireTarget);
		}
		else if (!Data.NPC->IsCurrentlyShooting()
			|| (Data.NPC->IsInBurstCooldown() && !bSuppressing))
		{
			// Same trap ShooterPush documents: the NPC restarts its own burst when the cooldown ends
			// without re-checking line of sight, so the task has to close the request explicitly and
			// let the next tick reopen it.
			//
			// Suppression is exempt from the burst pacing on purpose (author's call): the whole
			// value of covering fire is that it does not stop while a teammate is crossing open
			// ground, and a 1.5s silence in the middle of a 4s run is the run being uncovered for
			// most of it. Re-requesting every tick keeps the trigger down through the cooldown.
			StopShooting(Data);
		}
	}
	else if (Data.bIsShooting)
	{
		StopShooting(Data);
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ShooterPeek::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	UE_LOG(LogTemp, Warning, TEXT("[PEEK_DEBUG] %s EXIT Peek from phase %s"),
		*GetNameSafe(Data.NPC), PeekPhaseName(Data.Phase));

	ReleaseAll(Data);
}

bool FSTTask_ShooterPeek::TryMoveTo(FInstanceDataType& Data, const FVector& Location) const
{
	if (!Data.Controller)
	{
		return false;
	}

	FAIMoveRequest MoveRequest;
	MoveRequest.SetGoalLocation(Location);
	MoveRequest.SetAcceptanceRadius(Data.ArriveRadius);
	MoveRequest.SetUsePathfinding(true);
	MoveRequest.SetAllowPartialPath(true);
	MoveRequest.SetProjectGoalLocation(true);
	MoveRequest.SetCanStrafe(true);

	const FPathFollowingRequestResult Result = Data.Controller->MoveTo(MoveRequest);

	// AlreadyAtGoal counts as arrived here, unlike in the push: the H/P pair is only
	// PeekStepDistance apart, so a leg that is already inside the acceptance radius is the normal
	// case near the corner and not a degenerate destination.
	return Result.Code == EPathFollowingRequestResult::RequestSuccessful
		|| Result.Code == EPathFollowingRequestResult::AlreadyAtGoal;
}

void FSTTask_ShooterPeek::StartStrafeLeg(FInstanceDataType& Data, const FVector& Anchor) const
{
	// Rolled first, so even an early return still costs a leg's worth of time instead of retrying
	// the same failing leg every tick.
	Data.LegElapsed = 0.0f;
	Data.LegDuration = FMath::FRandRange(Data.StrafeHoldMin, Data.StrafeHoldMax);

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

	const FVector Origin = Data.NPC->GetActorLocation();

	FVector ToTarget = (Data.Target->GetActorLocation() - Origin).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero())
	{
		ToTarget = FVector::ForwardVector;
	}

	// Pure lateral, the same shape the duel uses in the push: no closing and no backing off, just
	// left and right across the target's line, so the NPC keeps its range while it fires.
	const FVector Lateral = FVector::CrossProduct(FVector::UpVector, ToTarget).GetSafeNormal();

	// Leg length follows from how fast this NPC actually moves, so retuning its speed does not
	// silently retune the shape of the strafe as well.
	const UCharacterMovementComponent* const Movement = Data.NPC->GetCharacterMovement();
	const float Speed = FMath::Max(100.0f, Movement ? Movement->GetMaxSpeed() : 400.0f);
	const float LegLength = Speed * Data.LegDuration;

	// Both sides, and at full length then at half. A leg into a wall is the normal case right
	// beside cover, and flipping is the cheapest answer for that. But a tight corner can refuse
	// BOTH directions at full length while still having room for a shorter step - measured on the
	// test level's actual corners, where the two full-length tries failed every single leg and the
	// NPC never moved at all for the rest of the peek, gun aimed at nothing changing. Shrinking
	// the reach before giving up costs nothing extra when the room is open: the first, full-length
	// try still wins there.
	for (const float LengthFraction : { 1.0f, 0.5f })
	{
		const float TryLength = LegLength * LengthFraction;

		for (int32 Attempt = 0; Attempt < 2; ++Attempt)
		{
			FVector Candidate = Origin + Lateral * Data.LegSign * TryLength;

			// Leash to the corner. P is a step out of cover, not a new position, so a leg that would
			// carry past PeekLeash is clamped back onto the circle around P instead of running on and
			// quietly turning the peek into a relocation.
			const FVector FromAnchor = Candidate - Anchor;
			if (FromAnchor.Size2D() > Data.PeekLeash)
			{
				Candidate = Anchor + FromAnchor.GetSafeNormal2D() * Data.PeekLeash;
			}

			// Already at the leash edge on this side: the clamp just handed back where it is standing,
			// which would be a no-op move and a leg spent motionless. Take the other side instead.
			if (FVector::Dist2D(Candidate, Origin) <= Data.ArriveRadius)
			{
				Data.LegSign = -Data.LegSign;
				continue;
			}

			FNavLocation NavResult;
			if (NavSys->ProjectPointToNavigation(Candidate, NavResult, FVector(200.0f, 200.0f, 300.0f)))
			{
				TryMoveTo(Data, NavResult.Location);
				return;
			}

			Data.LegSign = -Data.LegSign;
		}
	}
}

void FSTTask_ShooterPeek::EnterPhase(FInstanceDataType& Data, EShooterPeekPhase NewPhase) const
{
	UE_LOG(LogTemp, Warning, TEXT("[PEEK_DEBUG] %s phase %s -> %s (spent %.2fs)"),
		*GetNameSafe(Data.NPC), PeekPhaseName(Data.Phase), PeekPhaseName(NewPhase), Data.PhaseElapsed);

	Data.Phase = NewPhase;
	Data.PhaseElapsed = 0.0f;

	// A fresh goal, so the progress watchdog starts over. Seeded at the float maximum rather than at
	// zero: the first tick of the new phase must be able to count as an improvement, whatever the
	// distance to the new goal happens to be.
	Data.BestGoalDistance = TNumericLimits<float>::Max();
	Data.SinceGoalProgress = 0.0f;
}

/** How much closer counts as progress. Small enough that a slow walk still registers every tick,
 *  large enough that path-following jitter on the spot does not read as movement. */
static constexpr float ShooterPeek_ProgressEpsilon = 25.0f;


bool FSTTask_ShooterPeek::IsMoveStalled(FInstanceDataType& Data, float DeltaTime, float DistanceToGoal) const
{
	if (DistanceToGoal < Data.BestGoalDistance - ShooterPeek_ProgressEpsilon)
	{
		Data.BestGoalDistance = DistanceToGoal;
		Data.SinceGoalProgress = 0.0f;
		return false;
	}

	// Not the same thing as "the phase has been running a while". The old absolute clock cut walks
	// off in mid-stride, and since giving up also drops the cover claim, the next search handed out
	// a different corner and the NPC restarted the walk it was about to finish.
	Data.SinceGoalProgress += DeltaTime;
	return Data.SinceGoalProgress >= Data.MoveTimeout;
}

bool FSTTask_ShooterPeek::UpdateSuppressionDuty(FInstanceDataType& Data) const
{
	AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
	AShooterAIController* const ShooterController = Cast<AShooterAIController>(Data.Controller);

	APawn* const Wanted = (Coordinator && !Data.bIsRelocating)
		? Coordinator->GetSuppressionTarget(Data.NPC)
		: nullptr;

	if (Wanted)
	{
		if (Wanted != Data.SuppressionTarget)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SQUAD_DEBUG] %s SUPPRESSING %s"),
				*GetNameSafe(Data.NPC), *GetNameSafe(Wanted));
		}

		Data.SuppressionTarget = Wanted;

		// Through the distraction, not by writing the controller's target directly. The controller's
		// perception rewrites its target on every stimulus, so anything that merely SETS it loses
		// the argument within a frame; the distraction is the project's existing way of saying "and
		// do not let perception change it back".
		//
		// Refreshed every tick with a short deadline rather than set once with a long one. That is
		// how the coordinator already drives decoys, and it fails safe: if this task stops running
		// for any reason - death, a knockback, the tree moving elsewhere - the hold lapses within
		// the second instead of stranding the NPC staring at somebody indefinitely.
		if (ShooterController)
		{
			ShooterController->DistractTo(Wanted, 1.0f);
		}

		return true;
	}

	// Duty over. Dropping the distraction has to null the target as well, or the NPC keeps firing at
	// a player it is no longer assigned to: the sense task only looks for somebody new when it has
	// nobody, so leaving a stale one in place means it never re-acquires.
	Data.SuppressionTarget = nullptr;

	if (ShooterController && ShooterController->IsDistracted())
	{
		ShooterController->EndDistraction();

		UE_LOG(LogTemp, Warning, TEXT("[SQUAD_DEBUG] %s suppression ended"), *GetNameSafe(Data.NPC));
	}

	return false;
}

bool FSTTask_ShooterPeek::BeginRelocationRun(FInstanceDataType& Data, float Distance) const
{
	if (Data.bIsRelocating)
	{
		return true;
	}

	AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
	if (!Coordinator)
	{
		// No squad to tell. Run anyway - the sprint is worth having on its own, it is only the
		// covering fire that needs somebody to arrange it.
		Data.bIsRelocating = true;
		return true;
	}

	// Who took the corner away. Asking the cover component per player rather than assuming it was
	// the current target: in coop the one who flanked is usually not the one being shot at, and
	// naming the wrong player would point the covering fire at the wrong place.
	APawn* Opener = nullptr;
	if (const UCoverFinderComponent* const Finder = Data.NPC->FindComponentByClass<UCoverFinderComponent>())
	{
		TArray<APawn*> Players;
		CoopPlayers::GetAll(Data.NPC->GetWorld(), Players);

		float WorstThreat = -1.0f;
		for (APawn* const Player : Players)
		{
			if (!Finder->IsCoverOpenedBy(Player))
			{
				continue;
			}

			const float Threat = Coordinator->GetPlayerThreat(Player);
			if (Threat > WorstThreat)
			{
				WorstThreat = Threat;
				Opener = Player;
			}
		}
	}

	const UCharacterMovementComponent* const Movement = Data.NPC->GetCharacterMovement();
	const float Speed = FMath::Max(100.0f, Movement ? Movement->GetMaxSpeed() : 400.0f);
	const float ExpectedSeconds = Distance / Speed;

	if (!Coordinator->BeginRelocation(Data.NPC, Opener, ExpectedSeconds))
	{
		// Squad is already covering as many runs as it can. Keep the corner and try again next
		// cycle: leaving now would be the case where everybody runs and nobody covers.
		return false;
	}

	Data.bIsRelocating = true;

	// The body goes onto the direction of travel for the run, the same exception ShooterPush makes
	// for its charge and for the same reason - the run animation describes where it is going. It is
	// silent for the same stretch, which is what the covering fire is for.
	SetShooterRotationMode(Data.NPC, /*bFaceTarget*/ false);

	if (UApexMovementComponent* const Apex = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement()))
	{
		Apex->StartSprint();
	}

	return true;
}

void FSTTask_ShooterPeek::EndRelocationRun(FInstanceDataType& Data) const
{
	if (!Data.bIsRelocating)
	{
		return;
	}

	Data.bIsRelocating = false;

	if (AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC))
	{
		Coordinator->EndRelocation(Data.NPC);
	}

	if (UApexMovementComponent* const Apex = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement()))
	{
		Apex->StopSprint();
	}

	// Back onto the target. The per-tick call at the top of Tick would fix this anyway, but leaving
	// it to chance is how the mid-sprint shield break left Peek facing its own footsteps for a whole
	// peek once already.
	SetShooterRotationMode(Data.NPC, /*bFaceTarget*/ true);
}

bool FSTTask_ShooterPeek::HasPathFollowingGivenUp(const FInstanceDataType& Data) const
{
	// Grace period for the frame or two right after TryMoveTo, where Idle means "has not started"
	// rather than "gave up". EnterPhase resets PhaseElapsed to zero on every fresh goal, so this
	// reads the same clock the phase itself uses.
	if (Data.PhaseElapsed < 0.2f || !Data.Controller)
	{
		return false;
	}

	const UPathFollowingComponent* const PathComp = Data.Controller->GetPathFollowingComponent();
	return PathComp && PathComp->GetStatus() == EPathFollowingStatus::Idle;
}

void FSTTask_ShooterPeek::StartShooting(FInstanceDataType& Data, AActor* ShootAt) const
{
	if (!Data.NPC || !ShootAt)
	{
		return;
	}

	// The peek does NOT queue for an attack token, unlike the push. Author's call, and the reason is
	// that the two are asking different questions. The coordinator's token budget exists to stop a
	// crowd converging on one player and all firing at once; a peek is the opposite situation - the
	// NPC has already given up ground, committed to a corner, waited out a hide and deliberately
	// stepped into the open for a second or two. Making that trip and then standing there silent
	// because somebody else holds the token is the whole "walks out, does nothing, walks back" read.
	//
	// bHasExternalPermission is the existing mechanism for exactly this, not a new one:
	// AShooterNPC::RequestAttackPermission short-circuits on it (ShooterNPC.cpp), so the coordinator
	// is bypassed rather than fought with.
	Data.NPC->StartShooting(ShootAt, /*bHasExternalPermission*/ true);
	Data.bIsShooting = true;
}

void FSTTask_ShooterPeek::StopShooting(FInstanceDataType& Data) const
{
	if (Data.NPC)
	{
		Data.NPC->StopShooting();
	}
	Data.bIsShooting = false;
}

void FSTTask_ShooterPeek::ReleaseAll(FInstanceDataType& Data) const
{
	StopShooting(Data);

	// Both squad duties are claims on somebody else's behaviour, so both must be given back on EVERY
	// exit, death and recycling included - the same rule the cover claim already lives under, and
	// for the same reason. A leaked relocation permanently consumes one of the squad's runner slots,
	// and a leaked suppression leaves the NPC's perception locked onto a player it was told to watch
	// for a run that finished long ago.
	EndRelocationRun(Data);

	Data.SuppressionTarget = nullptr;
	if (AShooterAIController* const ShooterController = Cast<AShooterAIController>(Data.Controller))
	{
		if (ShooterController->IsDistracted())
		{
			ShooterController->EndDistraction();
		}
	}

	if (Data.NPC)
	{
		if (UCoverFinderComponent* const Finder = Data.NPC->FindComponentByClass<UCoverFinderComponent>())
		{
			Finder->ReleaseCover();
		}
	}

	if (Data.Controller)
	{
		Data.Controller->ClearFocus(EAIFocusPriority::Gameplay);
		Data.Controller->StopMovement();
	}
}

#if WITH_EDITOR
FText FSTTask_ShooterPeek::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	if (!Data)
	{
		return FText::FromString(TEXT("Peek from cover"));
	}

	return FText::FromString(FString::Printf(
		TEXT("Peek: hide %.1fs, step out and strafe-fire %.1fs (legs %.2f-%.2fs), repeat"),
		Data->HideDuration, Data->PeekDuration, Data->StrafeHoldMin, Data->StrafeHoldMax));
}
#endif
