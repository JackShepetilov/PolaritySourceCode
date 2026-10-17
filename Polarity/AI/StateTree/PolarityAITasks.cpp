// PolarityAITasks.cpp

#include "PolarityAITasks.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "../Coordination/AICombatCoordinator.h"
#include "../Components/MeleeRetreatComponent.h"
#include "../Components/CoverFinderComponent.h"
#include "../Components/AIAccuracyComponent.h"
#include "../FactionContactMemory.h"
#include "../../Variant_Shooter/Bench/FightRecorder.h"
#include "../../Variant_Shooter/Weapons/ShooterWeapon.h"
#include "../BodySamples.h"
#include "../../EMFVelocityModifier.h"
#include "../../Variant_Shooter/Shield/ShieldFieldComponent.h"
#include "AI/PolarityTeams.h"
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
#include "HAL/IConsoleManager.h"
#include "../../Variant_Shooter/AI/SquadSpawn/Squad.h"

static TAutoConsoleVariable<int32> CVarAIDuckByReason(
	TEXT("polarity.ai.duckbyreason"),
	1,
	TEXT("1: a soldier out from behind cover gets down only for a reason (suppressed, reloading, hit hard). 0: on the old clock."));

/** Where the one behind Controller thinks Who is; where Who stands when it has no such opinion.
 *  @see AShooterAIController::GetKnownPlaceOf */
static FVector KnownPlaceOf(const AAIController* Controller, const AActor* Who)
{
	FVector Place = Who ? Who->GetActorLocation() : FVector::ZeroVector;
	if (const AShooterAIController* const Brain = Cast<AShooterAIController>(Controller))
	{
		Brain->GetKnownPlaceOf(Who, Place);
	}
	return Place;
}

// A reaction to being hit or pinned, into the record of the fight, EVERY time. The word the soldier
// says goes through AShooterNPC::SayTactic, which drops a word said twice in a row, so a soldier
// sent down twice running left one line and the second read as "did not react" (2026-10-12).
// Dead ground: a target the soldier could SEE from a spot, standing, and could not SHOOT from it,
// because the barrel aimed down at him goes into the top of the wall it is standing behind. Under
// the wall of a nest, a roof, a tower. Measured 2026-10-12 on the sniper's nest: at 6 m the muzzle
// of a standing soldier, aimed at the player, was 31 cm below the top of its own parapet, and the
// soldier stood there looking at him with a trigger it could not pull.
// The body is a model, the same one AShooterNPC::HasLineOfSightTo uses for a lowered gun: eyes 160
// and shoulder 138 over the floor, the gun 105 long from the shoulder (all measured).
static bool IsInDeadGroundFrom(const AShooterNPC* Who, const FVector& FloorSpot, const AActor* Target, bool bAskTheGun)
{
	const UWorld* const World = Who ? Who->GetWorld() : nullptr;
	if (!World || !Target)
	{
		return false;
	}

	// Dead ground is "I see him and the gun cannot reach him". One that does not know where he is
	// does not see him: recorded 2026-10-12, four of a garrison said "he is under my wall" and
	// stayed down while the player was 10 to 30 m from where they thought he was.
	if (const AShooterAIController* const Brain = Cast<AShooterAIController>(Who->GetController()))
	{
		if (Target == Brain->GetCurrentTarget() && !Brain->Notices(Target))
		{
			return false;
		}
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(DeadGround), false, Who);
	Params.AddIgnoredActor(Target);

	BodySamples::FPoints Points;
	BodySamples::Get(Target, Points);

	// Seen from where its eyes would be, standing on that spot.
	const FVector Eyes = FloorSpot + FVector(0.0f, 0.0f, 160.0f);
	bool bSeen = false;
	for (const FVector& Point : Points)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Eyes, Point, ECC_Visibility, Params))
		{
			bSeen = true;
			break;
		}
	}
	if (!bSeen)
	{
		return false;   // not in view at all: that is hiding, not dead ground
	}
	if (!bAskTheGun)
	{
		return true;    // the caller has already found the gun has no line
	}

	// And the gun, as a model: it is pointed at the middle of the target (that is where the
	// controller's focus puts it), from a shoulder taken 8 cm low so that a barrel which would clear
	// the wall by a hair counts as one that does not. Standing up into "almost" is how a soldier ends
	// up on its feet in view with a trigger it cannot pull.
	const FVector Shoulder = FloorSpot + FVector(0.0f, 0.0f, 130.0f);
	const FVector Muzzle = Shoulder + (Target->GetActorLocation() - Shoulder).GetSafeNormal() * 105.0f;
	FHitResult BarrelHit;
	if (World->LineTraceSingleByChannel(BarrelHit, Shoulder, Muzzle, ECC_Visibility, Params))
	{
		return true;
	}
	for (const FVector& Point : Points)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Muzzle, Point, ECC_Visibility, Params))
		{
			return false;   // it can be shot from here
		}
	}
	return true;
}

static void NoteDuckReaction(const AShooterNPC* Who, const TCHAR* What)
{
	UFightRecorderSubsystem::NoteTactic(Who, FName(*FString::Printf(TEXT("DUCK: %s"), What)));
}

static TAutoConsoleVariable<int32> CVarAIAttackSlide(
	TEXT("polarity.ai.attackslide"),
	0,
	TEXT("1: a pushing enemy ends its charge in a slide at the target, as it used to. 0: it does not."));

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

		// The slide AT the target is switched off by default [author, 2026-10-11]: a slide is how an
		// enemy gets into cover now. polarity.ai.attackslide 1 brings the charge's slide back.
		if (CVarAIAttackSlide.GetValueOnGameThread() != 0)
		{
			Apex->StartSlideToPoint(Point, SteerRateDeg);
		}
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

		UE_LOG(LogTemp, Verbose, TEXT("[NAV_DEBUG] %s MoveWithStrafe: Result=%d, From=%s, To=%s, UsePathfinding=%d"),
			*PawnName, static_cast<int32>(Result.Code), *PawnLoc.ToString(), *Data.Destination.ToString(), Data.bUsePathfinding);

		// Check NavMesh at both locations
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Data.Controller->GetWorld());
		if (NavSys)
		{
			FNavLocation NavLoc;
			const bool bStartOnNav = NavSys->ProjectPointToNavigation(PawnLoc, NavLoc, FVector(50, 50, 200));
			const bool bDestOnNav = NavSys->ProjectPointToNavigation(Data.Destination, NavLoc, FVector(50, 50, 200));
			UE_LOG(LogTemp, Verbose, TEXT("[NAV_DEBUG] %s NavMesh check: StartOnNav=%d, DestOnNav=%d"),
				*PawnName, bStartOnNav, bDestOnNav);

			if (!bStartOnNav)
			{
				UE_LOG(LogTemp, Verbose, TEXT("[NAV_DEBUG] %s NPC LOCATION IS NOT ON NAVMESH! Loc=%s"), *PawnName, *PawnLoc.ToString());
			}
			if (!bDestOnNav)
			{
				UE_LOG(LogTemp, Verbose, TEXT("[NAV_DEBUG] %s DESTINATION IS NOT ON NAVMESH! Dest=%s"), *PawnName, *Data.Destination.ToString());
			}
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("[NAV_DEBUG] %s NO NAVIGATION SYSTEM FOUND!"), *PawnName);
		}
	}

	// Check immediate move result
	if (Result.Code == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[NAV_DEBUG] MoveWithStrafe: MoveTo FAILED immediately!"));
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

			UE_LOG(LogTemp, Verbose, TEXT("[NAV_DEBUG] %s MoveWithStrafe: PathFollowing is Idle - movement FAILED, dist=%.0f"),
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

// ---- The close fight ----
//
// What the cornered state is [author, 2026-10-11]. It used to be a charge: sprint at the target, slide
// round it, strafe in its face, which with two squads in one room read as enemies running circles
// round each other. Now an enemy with somebody on top of it, or with no corner left to take, does
// the one thing that makes sense at that range: it stands its ground, turns on whoever is nearest
// and empties the magazine into them without pausing between bursts. It sidesteps while it does, so
// it is not a post, and it gives a little ground to anybody inside arm's reach; it never closes.
// The tree takes it back to cover once nobody is near (AShooterNPC::CanRetakeCover).
//
// polarity.ai.attackslide 1 brings the old charge back in its place.
// TODO: a strike with the weapon once the magazine is dry. AShooterNPC has no melee of its own (that
// is AMeleeNPC), and one needs a montage.
namespace
{
	EStateTreeRunStatus ShooterPush_TickCloseFight(FSTTask_ShooterPush_Data& Data)
	{
		// Out of rounds and going in with the gun away: the strike owns the legs and the aim.
		// @see UNPCMeleeStrikeComponent
		if (Data.NPC->IsMeleeBusy())
		{
			return EStateTreeRunStatus::Running;
		}

		UWorld* const World = Data.NPC->GetWorld();
		const float Now = World->GetTimeSeconds();
		const FVector NPCLocation = Data.NPC->GetActorLocation();

		// Whoever is nearest, not whoever it was fighting before somebody got this close.
		AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
		AActor* Threat = Coordinator ? Coordinator->FindNearestHostile(Data.NPC) : nullptr;
		if (!Threat)
		{
			Threat = Data.Target;
		}

		Data.Controller->SetFocus(Threat);
		SetShooterRotationMode(Data.NPC, /*bFaceTarget*/ true);
		Data.NPC->SetDumpMagazine(true);

		if (Data.NPC->HasLineOfSightTo(Threat))
		{
			if (!Data.bIsShooting || !Data.NPC->IsCurrentlyShooting())
			{
				Data.NPC->StartShooting(Threat, /*bHasExternalPermission*/ true);
				Data.bIsShooting = true;
			}
		}
		else if (Data.bIsShooting)
		{
			Data.NPC->StopShooting();
			Data.bIsShooting = false;
		}

		Data.NPC->SayTactic(Data.NPC->IsReloadingWeapon() ? TEXT("CLOSE FIGHT: reloading") : TEXT("CLOSE FIGHT: emptying the magazine"));

		// The sidestep. Lateral to the threat, plus a step back when it is inside arm's reach.
		if (Now >= Data.LegEndTime)
		{
			Data.LegSign = -Data.LegSign;
			Data.LegEndTime = Now + FMath::FRandRange(0.5f, 0.9f);

			const FVector ToThreat = (Threat->GetActorLocation() - NPCLocation).GetSafeNormal2D();
			const float ThreatDistance = FVector::Dist2D(Threat->GetActorLocation(), NPCLocation);
			FVector Step = FVector::CrossProduct(FVector::UpVector, ToThreat) * Data.LegSign * 180.0f;
			if (ThreatDistance < 300.0f || Data.NPC->IsReloadingWeapon())
			{
				Step -= ToThreat * 200.0f;
			}
			else if (!Data.bIsShooting && Coordinator && Coordinator->GetPostureFor(Data.NPC) != ESquadPosture::Defense)
			{
				// Nobody in view: a sidestep on the spot is the enemy standing in a corner. Go and look,
				// by a path to where it thinks he is. A straight step at him walked into the wall between
				// them, over and over (CORNER bench, 2026-10-14).
				FVector Place = Threat->GetActorLocation();
				if (const AShooterAIController* const Brain = Cast<AShooterAIController>(Data.Controller))
				{
					Brain->GetKnownPlaceOf(Threat, Place);
				}
				Data.NPC->SayTactic(TEXT("CLOSE FIGHT: nobody in view, going to look"));
				Data.Controller->MoveToLocation(Place, 150.0f);
				return EStateTreeRunStatus::Running;
			}

			FNavLocation OnNav;
			UNavigationSystemV1* const NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (NavSys && NavSys->ProjectPointToNavigation(NPCLocation + Step, OnNav, FVector(150.0f, 150.0f, 250.0f)))
			{
				FAIMoveRequest MoveRequest;
				MoveRequest.SetGoalLocation(OnNav.Location);
				MoveRequest.SetAcceptanceRadius(30.0f);
				MoveRequest.SetCanStrafe(true);
				Data.Controller->MoveTo(MoveRequest);
			}
		}

		return EStateTreeRunStatus::Running;
	}
}

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

	if (Data.bNeverWithdraw && CVarAIAttackSlide.GetValueOnGameThread() == 0)
	{
		// The close fight: no charge to set up. See ShooterPush_TickCloseFight.
		Data.NPC->NotifyLastStandBegan();
		Data.bIsShooting = false;
		Data.LegEndTime = 0.0f;
		Data.LegSign = FMath::RandBool() ? 1.0f : -1.0f;
		Data.Phase = Data.PreviousPhase = EShooterPushPhase::Duel;

		if (UApexMovementComponent* const Apex = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement()))
		{
			Apex->StopSprint();
		}
		return EStateTreeRunStatus::Running;
	}

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
		Data.NPC->NotifyLastStandBegan();
		bLastStandOrbit = HasRoomToOrbit(Data);
		Data.Phase = bLastStandOrbit ? EShooterPushPhase::Sprint : EShooterPushPhase::Approach;

		UE_LOG(LogTemp, Warning, TEXT("[PUSH_DEBUG] %s LAST STAND branch: %s"),
			*GetNameSafe(Data.NPC), bLastStandOrbit ? TEXT("charge+orbit (open)") : TEXT("straight approach (tight)"));
	}
	else
	{
		Data.NPC->SayTactic(TEXT("RUSH: target is hurt"));
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

	if (Data.bNeverWithdraw && CVarAIAttackSlide.GetValueOnGameThread() == 0)
	{
		return ShooterPush_TickCloseFight(Data);
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
		if (Apex && (Data.PreviousPhase == EShooterPushPhase::Sprint
			|| Data.PreviousPhase == EShooterPushPhase::Approach))
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

	// Unseen approach is travel, not the committed close-range charge. LOS can change without
	// a phase edge, so update the sprint key every tick and release it as soon as combat is visible.
	const bool bHasLOS = Data.NPC->HasLineOfSightTo(Data.Target);
	const UEnemyCombatProfile* const ApproachProfile = Data.NPC->GetCombatProfile();
	const bool bSprintApproach = Data.Phase == EShooterPushPhase::Approach
		&& !Data.bNeverWithdraw && !bHasLOS
		&& (!ApproachProfile || ApproachProfile->bSprintWhenPushing);
	if (Data.Phase == EShooterPushPhase::Approach)
	{
		if (UApexMovementComponent* const ApexApproach = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement()))
		{
			if (bSprintApproach && !ApexApproach->IsSliding())
			{
				ApexApproach->StartSprint();
			}
			else
			{
				ApexApproach->StopSprint();
			}
		}
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
	const bool bChargingOnFoot = (bSprintApproach || (Data.Phase == EShooterPushPhase::Sprint
		&& !Data.bFireWhileSprinting))
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
				bHasLOS ? 1 : 0,
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

	if (bWantsFire && bHasLOS)
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

	if (Data.NPC)
	{
		Data.NPC->SetDumpMagazine(false);
	}
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
		// A sprint lowers the weapon, so it is not STARTED in front of the target: seen, the NPC keeps
		// walking in with the gun up [author, 2026-10-10]. Asked on the way in only. A sprint already
		// running is finished, or the phase would flip on every corner the line of sight crosses.
		if (Data.PreviousPhase != EShooterPushPhase::Sprint && Data.NPC->HasLineOfSightTo(Data.Target))
		{
			return EShooterPushPhase::Approach;
		}
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

	// Only the leg this task started. Cancelling everything is how an exit from this state tore down
	// whatever else was driving the pawn - a squad's march order, most of the time - and this state
	// is left and re-entered several times a second.
	if (Data.Controller && Data.bHasLeg)
	{
		Data.Controller->StopMovement();
	}
	Data.bHasLeg = false;
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

bool FSTCondition_ShooterShieldDown::TestCondition(FStateTreeExecutionContext& Context) const
{
	// The old shield rule - "the shield broke, run to cover" - is gone by design (author, 2026-09-17).
	// A player who breaks the shield no longer scares the NPC into the Peek corner: it keeps pushing
	// and fighting. Cover is now ONLY ever taken from sentry turrets, and that behaviour lives in
	// AShooterNPC::TickTurretCover rather than in this tree, so this condition's slot in the tree
	// simply never fires. Kept as a named condition so the tree asset keeps its shape.
	return false;
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

	// No shield component means the mechanic never applied, and such an NPC must keep pushing, so it
	// reads as shielded here. Same reasoning as the comment on the pair in the header.
	if (!IsValid(Data.NPC))
	{
		return true;
	}

	const UShieldFieldComponent* const Shield = UShieldFieldStatics::GetShieldField(Data.NPC);
	if (!Shield)
	{
		return true;
	}

	// Deliberately NOT !IsAtMaxCharge(): see the header. The ceiling is where the shield BREAKS; it
	// counts as restored only after the meter has come back down to a fraction of that ceiling.
	// The threshold itself is unchanged - what changed is that this arithmetic left the predicate
	// and now lives on the component, which is the same one the weapon's damage gate asks.
	const bool bUp = Shield->GetStrippedFraction() <= RecoveredFraction;

	// Deliberately NOT logged. A transition condition is evaluated every tick of every NPC, so a log
	// here produced tens of lines per second per enemy and buried every other diagnostic in the
	// file - which is exactly what it did while the grenadier bug was being chased. If this needs
	// measuring again, log it on the EDGE from the task that acts on it, not from the predicate.
	return bUp;
}

// ============================================================================
// Which fight
// ============================================================================

namespace
{
	bool ShooterWantsOpenFight(AShooterNPC* NPC)
	{
		AAICombatCoordinator* const Coordinator = IsValid(NPC) ? AAICombatCoordinator::GetCoordinator(NPC) : nullptr;
		return Coordinator && Coordinator->WantsRush(NPC);
	}
}

bool FSTCondition_ShooterWantsOpenFight::TestCondition(FStateTreeExecutionContext& Context) const
{
	return ShooterWantsOpenFight(Context.GetInstanceData(*this).NPC);
}

bool FSTCondition_ShooterWantsCover::TestCondition(FStateTreeExecutionContext& Context) const
{
	return !ShooterWantsOpenFight(Context.GetInstanceData(*this).NPC);
}

bool FSTCondition_ShooterCanRetakeCover::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AShooterNPC* const NPC = Context.GetInstanceData(*this).NPC;
	return IsValid(NPC) && NPC->CanRetakeCover();
}

#if WITH_EDITOR
FText FSTCondition_ShooterWantsOpenFight::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Coordinator says close on the hurt target"));
}

FText FSTCondition_ShooterWantsCover::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("No reason to fight in the open"));
}

FText FSTCondition_ShooterCanRetakeCover::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Cornered charge has run its time and nobody is close"));
}
#endif

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

/** Inside this distance of a LOW hide point the NPC goes down while still walking to it. About the
 *  length of a side peek's step out, so a return from the peek is crouched from its first step. */
static constexpr float ShooterPeek_LowCoverDuckDistance = 350.0f;

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

	UE_LOG(LogTemp, Verbose, TEXT("[PEEK_DEBUG] %s ENTER Peek (target %s)"),
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
	Data.bBoundPending = false;
	Data.bBoundFlank = false;
	Data.bRunDecided = false;
	Data.bEntrySlideStarted = false;
	Data.BlindPeeks = 0;
	Data.FlankedChecks = 0;
	Data.OpenBlindSeconds = 0.0f;

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

	// Nobody of the side has seen him for a while: one of them walks to the place he was lost at and
	// looks round from there, the rest keep their guns on it. That one's legs and eyes are the
	// controller's until somebody sees him again. @see AShooterAIController::TickSearchForTarget
	if (AShooterAIController* const Brain = Cast<AShooterAIController>(Data.Controller))
	{
		if (Brain->TickSearchForTarget(!bSuppressing && !Data.bIsRelocating))
		{
			if (Data.bIsShooting)
			{
				StopShooting(Data);
			}
			if (Finder->HasCover())
			{
				Finder->ReleaseCover();
			}
			if (Data.Phase != EShooterPeekPhase::Seeking)
			{
				EnterPhase(Data, EShooterPeekPhase::Seeking);
			}
			return EStateTreeRunStatus::Running;
		}
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

		// The search of a bound came back with the corner it left from: nothing over there. Keep the
		// corner and ask again later, on a shorter clock than the first time.
		if (Data.bBoundPending && Finder->HasCover()
			&& FVector::Dist2D(Finder->GetCover().HideLocation, Data.BoundFromHide) < 50.0f)
		{
			Data.bBoundPending = false;
			if (AAICombatCoordinator* const SlotCoordinator = AAICombatCoordinator::GetCoordinator(Data.NPC))
			{
				SlotCoordinator->EndRelocation(Data.NPC);
			}
			Data.NPC->SayTactic(TEXT("NOWHERE to go, holding"));
		}
	}
	Data.bWasSearching = bSearchingNow;

	// ---- Cornered ----
	//
	// Two ways out of the peek and into the charge, and both mean the same thing: there is no
	// hiding left to do. Succeeded is the signal; the tree owns what happens next.
	if (Data.FailedSearches >= Data.LastStandFailedSearches)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[PEEK_DEBUG] %s -> LAST STAND: %d failed searches"),
			*GetNameSafe(Data.NPC), Data.FailedSearches);
		return EStateTreeRunStatus::Succeeded;
	}

	if (Data.LastStandPlayerDistance > 0.0f)
	{
		// Nearest ENEMY, not the current target: somebody flanking the corner has pushed it just as
		// much as the one being shot at, and in coop that is usually a different person. Not limited
		// to players any more, because a rifleman of the other faction closing in is the same kind of
		// pressure and should end the peek the same way.
		//
		// Through the coordinator because this runs every tick of the peek: its list is the players
		// plus the registered NPCs, which is two arrays rather than a sweep over the level.
		AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
		const APawn* Nearest = Coordinator ? Coordinator->FindNearestHostile(Data.NPC) : nullptr;
		if (!Nearest)
		{
			Nearest = PolarityTeams::FindNearestHostilePawn(Data.NPC);
		}

		if (Nearest)
		{
			// Close AND in view. Close through a wall is not somebody on top of it: the CORNER bench
			// (2026-10-14) had a soldier go into the close fight with its enemy 2 m behind a wall, and
			// in 3 of 8 fights nobody fired for 30 s, the one in the close fight stepping into the wall.
			const float PlayerDist = FVector::Dist2D(Nearest->GetActorLocation(), Data.NPC->GetActorLocation());
			if (PlayerDist <= Data.LastStandPlayerDistance && Data.NPC->HasLineOfSightTo(const_cast<APawn*>(Nearest)))
			{
				UE_LOG(LogTemp, Verbose, TEXT("[PEEK_DEBUG] %s -> LAST STAND: enemy %s at %.0f (limit %.0f)"),
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
			// On the defensive the corner to take is the one at hand: every metre from where it stands
			// costs, so the search does not send it across the field to a slightly better wall.
			AAICombatCoordinator* const SeekCoordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
			const bool bSeekNear = SeekCoordinator && SeekCoordinator->GetPostureFor(Data.NPC) == ESquadPosture::Defense;
			if (bSeekNear)
			{
				Finder->SetSearchBias(Data.NPC->GetActorLocation(), 0.3f, 0.0f);
			}
			if (!Finder->RequestCover(Data.Target) && bSeekNear)
			{
				Finder->ClearSearchBias();
			}
		}

		// No corner is no reason to hold fire: it is standing in the open with a target in view, which
		// is the one situation where shooting is all there is to do. Same pacing as the block at the
		// end of this function, which this return skips.
		// Reloading in the open, in view: back away from the target while the hands are busy, a leg
		// at a time. There is no corner to go to (that is why it is here), but standing still on the
		// spot it emptied the magazine from is the worst of what is left.
		Data.LegElapsed += DeltaTime;

		// Hit hard with no corner to get down behind: off the line it is being shot along, a few
		// metres back and to the side, at once. The reason is the same one that sends a soldier down
		// behind its cover; standing where it was just hit is not an answer to it [author,
		// 2026-10-11: "not all of them get down when hit"].
		const UAIAccuracyComponent* const OpenPinned = Data.NPC->FindComponentByClass<UAIAccuracyComponent>();
		const bool bOpenPinned = OpenPinned && OpenPinned->IsPinnedDown() && Data.LegElapsed >= 1.2f;
		if (CVarAIDuckByReason.GetValueOnAnyThread() != 0 && FireTarget
			&& (Data.NPC->GetRecentDamage() >= Data.DuckDamageThreshold || bOpenPinned))
		{
			Data.NPC->ClearRecentDamage();
			Data.LegElapsed = 0.0f;
			Data.LegSign = -Data.LegSign;
			const FVector HitAway = (Data.NPC->GetActorLocation() - KnownPlaceOf(Data.Controller, FireTarget)).GetSafeNormal2D();
			const FVector HitSide = FVector::CrossProduct(FVector::UpVector, HitAway) * Data.LegSign;
			FNavLocation HitNav;
			UNavigationSystemV1* const HitNavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Data.NPC->GetWorld());
			if (HitNavSys && HitNavSys->ProjectPointToNavigation(Data.NPC->GetActorLocation() + HitAway * 200.0f + HitSide * 350.0f,
				HitNav, FVector(200.0f, 200.0f, 300.0f)) && Data.NPC->IsInsideHoldArea(HitNav.Location))
			{
				TryMoveTo(Data, HitNav.Location);
				Data.NPC->SayTactic(bOpenPinned ? TEXT("PINNED in the open: off the line") : TEXT("HIT in the open: off the line"));
				NoteDuckReaction(Data.NPC, bOpenPinned ? TEXT("pinned, in the open") : TEXT("hit, in the open"));
			}
		}

		if (Data.NPC->IsReloadingWeapon() && FireTarget && Data.LegElapsed >= 0.8f && Data.NPC->HasLineOfSightTo(FireTarget))
		{
			Data.LegElapsed = 0.0f;
			const FVector Away = (Data.NPC->GetActorLocation() - FireTarget->GetActorLocation()).GetSafeNormal2D();
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Away) * Data.LegSign;
			FNavLocation OnNav;
			UNavigationSystemV1* const NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Data.NPC->GetWorld());
			if (NavSys && NavSys->ProjectPointToNavigation(Data.NPC->GetActorLocation() + Away * 350.0f + Side * 150.0f,
				OnNav, FVector(200.0f, 200.0f, 300.0f)) && Data.NPC->IsInsideHoldArea(OnNav.Location))
			{
				TryMoveTo(Data, OnNav.Location);
				Data.NPC->SayTactic(TEXT("RELOADING in the open, backing off"));
			}
		}

		const UEnemyCombatProfile* const OpenProfile = Data.NPC->GetCombatProfile();
		const bool bTooClose = OpenProfile && OpenProfile->MinFireDistance > 0.0f && FireTarget
			&& FVector::Dist2D(FireTarget->GetActorLocation(), Data.NPC->GetActorLocation()) < OpenProfile->MinFireDistance;
		const bool bOpenSees = FireTarget && Data.NPC->HasLineOfSightTo(FireTarget);

		// No corner AND no line: nothing to do here at all, and standing in it is the enemy "stuck in
		// a corner doing nothing". An advancing one walks at the target until it has a line; one that
		// holds stays where it was put.
		// Who may: the going half of an assault at once; anybody else only after it has stood blind
		// for a while (a firefight is fought from positions, and "I cannot see him" is a reason to look
		// for a corner first, which the search above is doing); a squad on the defensive never.
		Data.OpenBlindSeconds = bOpenSees ? 0.0f : Data.OpenBlindSeconds + DeltaTime;
		AAICombatCoordinator* const OpenCoordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
		const bool bMayClose = OpenCoordinator && OpenCoordinator->GetPostureFor(Data.NPC) != ESquadPosture::Defense
			&& (OpenCoordinator->IsInAssaultElement(Data.NPC) || Data.OpenBlindSeconds >= 4.0f);

		if (!bOpenSees && FireTarget && !Data.NPC->IsReloadingWeapon() && bMayClose && Data.LegElapsed >= 1.0f)
		{
			Data.LegElapsed = 0.0f;
			Data.bClosingToSee = TryMoveTo(Data, KnownPlaceOf(Data.Controller, FireTarget));
			Data.NPC->SayTactic(TEXT("NO COVER, NO ANGLE: closing to see the target"));
		}
		else if (!bOpenSees && FireTarget && !Data.NPC->IsReloadingWeapon() && Data.NPC->HasHoldArea()
			&& Data.OpenBlindSeconds >= 1.5f && Data.LegElapsed >= 2.0f)
		{
			// One that holds a ground does not close in, and must not stand blind either: it goes along
			// its own ground, to the edge on the side the enemy is on, left of the line and then right
			// of it, until it has a line. Recorded 2026-10-11: with nothing left to send it down and up
			// again, three of five stood for a whole fight behind a fence they could not see through.
			Data.LegElapsed = 0.0f;
			Data.LegSign = -Data.LegSign;
			if (Data.OpenBlindSeconds >= Data.WidenHoldAfterBlindSeconds && Data.NPC->CanWidenHoldArea())
			{
				// Its own yard has no line to the fight at all: the whole place is its ground now.
				Data.NPC->WidenHoldArea();
				Data.NPC->SayTactic(TEXT("HOLD: my ground sees nobody, taking the whole place"));
			}
			else
			{
				Data.NPC->SayTactic(TEXT("HOLD: no line from here, moving along my ground"));
			}
			TryMoveTo(Data, Data.NPC->GetHoldPointToward(KnownPlaceOf(Data.Controller, FireTarget), 300.0f * Data.LegSign));
		}
		else if (bOpenSees && Data.bClosingToSee)
		{
			Data.bClosingToSee = false;
			Data.Controller->StopMovement();
		}

		if (FireTarget && !bTooClose && bOpenSees)
		{
			Data.NPC->SayTactic(TEXT("NO COVER, shooting from the open"));
			if (!Data.bIsShooting)
			{
				StartShooting(Data, FireTarget);
			}
			else if (!Data.NPC->IsCurrentlyShooting() || Data.NPC->IsInBurstCooldown())
			{
				StopShooting(Data);
			}
		}
		else if (Data.bIsShooting)
		{
			StopShooting(Data);
		}

		return EStateTreeRunStatus::Running;
	}

	const FCoverSpot& Spot = Finder->GetCover();

	// A fresh spot arrived while Seeking: start walking to its hide end.
	if (Data.Phase == EShooterPeekPhase::Seeking)
	{
		// A NEW spot, so this NPC owes it one peek before the recheck is allowed to give up on it.
		Data.bPeekedSinceCover = false;
		Data.BlindPeeks = 0;
		Data.FlankedChecks = 0;

		// New corner, new clock and a clean sheet on having fired from it. Both are per POSITION,
		// which is what makes "never shoot twice from the same place" mean anything.
		Data.CornerElapsed = 0.0f;
		Data.bFiredFromCorner = false;

		EnterPhase(Data, EShooterPeekPhase::ToHide);
		TryMoveTo(Data, Spot.HideLocation);
		return EStateTreeRunStatus::Running;
	}

	const FVector NPCLocation = Data.NPC->GetActorLocation();

	// Whoever CAN be shot from here while the one it is fighting cannot: the enemy who came round the
	// back. A corner is for shooting from, at whoever shows.
	AActor* Opportunity = nullptr;
	if (!bSuppressing && FireTarget && !Data.bIsRelocating)
	{
		Data.SinceOpportunityCheck += DeltaTime;
		if (Data.SinceOpportunityCheck >= 0.3f)
		{
			Data.SinceOpportunityCheck = 0.0f;
			AAICombatCoordinator* const EyesCoordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
			Data.OpportunityTarget = (!EyesCoordinator || Data.NPC->HasLineOfSightTo(FireTarget))
				? nullptr : EyesCoordinator->FindVisibleHostile(Data.NPC);
		}
		Opportunity = Data.OpportunityTarget.Get();
	}

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
		// Run or walk, decided once per walk (see the header: unseen runs, seen and short walks with the
		// gun up, seen and long runs only under a mate's fire).
		if (!Data.bRunDecided)
		{
			Data.bRunDecided = true;

			AAICombatCoordinator* const RunCoordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
			const bool bSeen = Data.NPC->HasLineOfSightTo(Data.Target);
			const bool bCovered = RunCoordinator && RunCoordinator->HasCoveringMate(Data.NPC);
			const bool bRun = ToHideDistance > Data.BoundSprintDistance && (!bSeen || bCovered);

			if (bRun && BeginRelocationRun(Data, ToHideDistance))
			{
				Data.NPC->SayTactic(!bSeen ? TEXT("RUNNING unseen")
					: (Data.bBoundFlank ? TEXT("FLANKING (run, gun down, covered)") : TEXT("RUNNING (gun down, covered)")));
			}
			else if (RunCoordinator && Data.bBoundPending)
			{
				// Walking it: the mover slot the bound took is not needed for a walk with the gun up.
				RunCoordinator->EndRelocation(Data.NPC);
			}
		}

		// Walking, and rounds are going past or it has just been hit: the rest is run. A walk with the
		// gun up is for ground nobody is shooting across. Recorded 2026-10-11: a quarter of the
		// player's hits landed on a soldier walking to its corner.
		if (!Data.bIsRelocating && CVarAIDuckByReason.GetValueOnAnyThread() != 0 && ToHideDistance > 150.0f)
		{
			const UAIAccuracyComponent* const WalkPinned = Data.NPC->FindComponentByClass<UAIAccuracyComponent>();
			if (((WalkPinned && WalkPinned->IsPinnedDown()) || Data.NPC->GetRecentDamage() >= Data.DuckDamageThreshold)
				&& BeginRelocationRun(Data, ToHideDistance))
			{
				Data.NPC->SayTactic(TEXT("UNDER FIRE: running to cover"));
				NoteDuckReaction(Data.NPC, TEXT("under fire on the walk, runs"));
			}
		}

		if (Data.bIsRelocating)
		{
			UApexMovementComponent* const RunApex = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement());

			// Broken off: the target is close, in front and in view. Gun up, the rest is walked.
			const FVector ToTarget = Data.Target->GetActorLocation() - NPCLocation;
			const bool bTargetAhead = FVector::DotProduct(ToTarget.GetSafeNormal2D(), Data.NPC->GetVelocity().GetSafeNormal2D()) > 0.3f;
			if (ToTarget.Size2D() < Data.RunBreakDistance && bTargetAhead && Data.NPC->HasLineOfSightTo(Data.Target))
			{
				EndRelocationRun(Data);
				Data.NPC->SayTactic(TEXT("RUN BROKEN: target close in front"));
			}
			// The last stretch is slid: faster through the open, and it arrives low.
			else if (!Data.bEntrySlideStarted && RunApex && Data.SlideEntryDistance > 0.0f
				&& ToHideDistance < Data.SlideEntryDistance && ToHideDistance > 150.0f)
			{
				RunApex->StartSlideToPoint(Spot.HideLocation, Data.SlideEntrySteerRate);
				if (RunApex->IsSliding())
				{
					RunApex->Velocity = FVector(RunApex->Velocity.X * Data.SlideEntrySpeedScale,
						RunApex->Velocity.Y * Data.SlideEntrySpeedScale, RunApex->Velocity.Z);
					Data.bEntrySlideStarted = true;
					RunApex->StopSprint();
					Data.NPC->SayTactic(TEXT("SLIDING into cover"));
				}
			}
			else
			{
				// The corner it is running to stopped being one (the target moved): pick again.
				Data.SinceRecheck += DeltaTime;
				if (Data.SinceRecheck >= 0.5f)
				{
					Data.SinceRecheck = 0.0f;
					if (!Finder->IsCoverStillGood())
					{
						Data.bBoundPending = false;
						EndRelocationRun(Data);
						Finder->ReleaseCover();
						Data.Controller->StopMovement();
						EnterPhase(Data, EShooterPeekPhase::Seeking);
						break;
					}
				}
			}
		}

		// Low cover hides a crouched body only, so the last steps to it are taken low [author,
		// 2026-10-12]. Until now the crouch waited for AtHide: an NPC that ran dry out at the peek stood
		// up (EnterPhase), walked back and did the first part of its reload standing over the wall.
		// Not on a run: the sprint and the slide into cover own the pose there.
		if (Spot.bLowCover && !Data.bIsRelocating && ToHideDistance <= ShooterPeek_LowCoverDuckDistance)
		{
			UApexMovementComponent* const DuckApex = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement());
			if (DuckApex && !DuckApex->IsSliding() && !DuckApex->IsCrouching())
			{
				DuckApex->StartCrouching();
				UE_LOG(LogTemp, Verbose, TEXT("[PEEK_DEBUG] %s ducks on the way back to low cover, %.0f left"),
					*GetNameSafe(Data.NPC), ToHideDistance);
			}
		}

		if (ToHideDistance <= ShooterPeek_EffectiveArriveRadius(Data))
		{
			Data.bBoundPending = false;
			EndRelocationRun(Data);
			Data.Controller->StopMovement();
			EnterPhase(Data, EShooterPeekPhase::AtHide);
		}
		else if (IsMoveStalled(Data, DeltaTime, ToHideDistance))
		{
			// Genuinely stuck, not merely far. Drop the claim so the corner is not held by an NPC
			// that cannot reach it, and go find another one.
			UE_LOG(LogTemp, Verbose, TEXT("[PEEK_DEBUG] %s STALLED walking to H, %.0f left (best %.0f, arrive<=%.0f)"),
				*GetNameSafe(Data.NPC), ToHideDistance, Data.BestGoalDistance, ShooterPeek_EffectiveArriveRadius(Data));

			Data.bBoundPending = false;
			EndRelocationRun(Data);
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
				UE_LOG(LogTemp, Verbose, TEXT("[PEEK_DEBUG] %s fired from this corner, moving on (held %.1fs)"),
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
		// A laser held on it or by it for long [author, 2026-10-15]: this corner is known, another one.
		if (const UAIAccuracyComponent* const PaintAccuracy = Data.NPC->FindComponentByClass<UAIAccuracyComponent>();
			PaintAccuracy && PaintAccuracy->GetPaintedSeconds() >= PaintAccuracy->PaintedRelocateSeconds && !Finder->IsSearching())
		{
			Data.NPC->SayTactic(TEXT("PAINTED: giving the corner up"));
			Finder->ReleaseCover();
			EnterPhase(Data, EShooterPeekPhase::Seeking);
			break;
		}

		Data.SinceRecheck += DeltaTime;
		if (Data.bPeekedSinceCover && Data.SinceRecheck >= Data.CoverRecheckInterval)
		{
			Data.SinceRecheck = 0.0f;

			// A corner is held MinCornerHold before "a little more exposed than it was" may retire it.
			// Walked round is different: its wall is gone, and it goes at once.
			Data.FlankedChecks = Finder->IsFlanked() ? Data.FlankedChecks + 1 : 0;
			const bool bCornerLost = Data.FlankedChecks >= 2
				|| (Data.CornerElapsed >= Data.MinCornerHold && !Finder->IsCoverStillGood());
			if (bCornerLost)
			{
				UE_LOG(LogTemp, Verbose,
					TEXT("[PEEK_DEBUG] %s DROPPED cover at AtHide: exposure %.2f over threshold, sat %.2fs of %.2fs"),
					*GetNameSafe(Data.NPC), Finder->EvaluateCurrentExposure(), Data.PhaseElapsed, Data.HideDuration);

				Finder->ReleaseCover();
				EnterPhase(Data, EShooterPeekPhase::Seeking);
				break;
			}
		}

		// ---- Assault: the going half ----
		//
		// The squad is three to one or better and this NPC is one of the half that goes in. After a
		// couple of seconds on a corner it asks for its next one, toward its lane (the front, or round
		// one side). The search is the ordinary one, leaned that way and allowed closer, so what it
		// goes to is still a corner. Two go at a time; the rest of the going half waits its turn and
		// the staying half fires.
		if (!Data.bBoundPending && !bSuppressing && Data.CornerElapsed >= 2.0f
			&& !Finder->IsSearching() && Finder->GetRequeryCooldownRemaining() <= 0.0f)
		{
			AAICombatCoordinator* const AssaultCoordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
			FVector Preferred = FVector::ZeroVector;
			bool bFlank = false;
			if (AssaultCoordinator && AssaultCoordinator->IsInAssaultElement(Data.NPC)
				&& AssaultCoordinator->RequestAssaultMove(Data.NPC, Preferred, bFlank))
			{
				Finder->SetSearchBias(Preferred, Data.BoundBiasPerMetre, Data.BoundMinMove, Data.AssaultMinTargetDistance);
				if (Finder->RequestCover(Data.Target))
				{
					Data.bBoundPending = true;
					Data.bBoundFlank = bFlank;
					Data.BoundFromHide = Spot.HideLocation;
					Data.NPC->SayTactic(bFlank ? TEXT("ASSAULT: going round the side") : TEXT("ASSAULT: going forward"));
				}
				else
				{
					Finder->ClearSearchBias();
					AssaultCoordinator->EndRelocation(Data.NPC);
				}
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
			// A different corner, so its one owed peek starts over with it, and so does its clock.
			Data.bPeekedSinceCover = false;
			Data.CornerElapsed = 0.0f;
			Data.bFiredFromCorner = false;

			EnterPhase(Data, EShooterPeekPhase::ToHide);
			TryMoveTo(Data, Spot.HideLocation);
			break;
		}

		// Its bound is being looked for: stay down until the answer is in, so the run starts from
		// behind the corner and not from half way out of it.
		if (Data.bBoundPending && Finder->IsSearching())
		{
			break;
		}

		// Behind the corner with nobody looking and a magazine that is not full: fill it now. The
		// alternative is running dry out at P, which is the one place a reload cannot be afforded.
		if (Data.NPC->GetMagazineFraction() < Data.TacticalReloadBelow && !Data.NPC->IsReloadingWeapon()
			&& !Data.NPC->HasLineOfSightTo(Data.Target) && Data.NPC->RequestTacticalReload())
		{
			Data.NPC->SayTactic(TEXT("RELOADING in cover (magazine not empty)"));
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

		// How long it stays down. On the clock: HideDuration. By reason: until what sent it down is
		// over, and while rounds that suppress keep coming the time runs from the last of them.
		const bool bByReason = CVarAIDuckByReason.GetValueOnAnyThread() != 0;
		const float HideNow = Data.NPC->GetWorld() ? Data.NPC->GetWorld()->GetTimeSeconds() : 0.0f;
		const UAIAccuracyComponent* const HideAccuracy = Data.NPC->FindComponentByClass<UAIAccuracyComponent>();
		if (bByReason && HideAccuracy && HideAccuracy->IsPinnedDown())
		{
			Data.DuckUntil = FMath::Max(Data.DuckUntil, HideNow + Data.DuckSecondsOnSuppression);
		}

		// Sent down: it comes up at the nearest other spot of cover, not where the one who sent it
		// down is holding his sight. The search is the ordinary one, leaned hard toward where it is;
		// its answer moves the corner, and the test above ("the corner moved") walks it there.
		if (bByReason && Data.bShiftAfterDuck && !Finder->IsSearching() && Finder->GetRequeryCooldownRemaining() <= 0.0f)
		{
			Data.bShiftAfterDuck = false;
			if (Data.ShiftAfterDuckDistance > 0.0f)
			{
				Finder->SetSearchBias(Spot.HideLocation, 1.0f, Data.ShiftAfterDuckDistance);
				if (!Finder->RequestCover(Data.Target))
				{
					Finder->ClearSearchBias();
				}
			}
		}

		bool bHiddenLongEnough = bByReason
			? (HideNow >= Data.DuckUntil && Data.PhaseElapsed >= Data.MinHideSeconds && !Finder->IsSearching())
			: Data.PhaseElapsed >= Data.HideDuration;

		// He is under my wall: coming up would be standing in his view with a gun that cannot reach
		// him. It stays down until he is out of the dead ground [author, 2026-10-12]. Asked of the
		// peek point as the floor it would stand on. @see IsInDeadGroundFrom
		if (bHiddenLongEnough && Data.Target
			&& IsInDeadGroundFrom(Data.NPC, Spot.PeekLocation, Data.Target, /*bAskTheGun*/ true))
		{
			bHiddenLongEnough = false;
			Data.NPC->SayTactic(TEXT("DEAD GROUND: he is under my wall, staying down"));
		}

		if (bHiddenLongEnough && bWeaponReady)
		{
			Data.DuckUntil = 0.0f;
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
				UE_LOG(LogTemp, Verbose, TEXT("[PEEK_DEBUG] %s step-out REFUSED, dropping corner"),
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
		// An empty gun in the open has one useful thing to do, which is to stop being in the open.
		if (Data.NPC->IsReloadingWeapon() && !bSuppressing)
		{
			Data.NPC->SayTactic(TEXT("RELOADING, back to cover"));
			EnterPhase(Data, EShooterPeekPhase::ToHide);
			TryMoveTo(Data, Spot.HideLocation);
			break;
		}

		// Hit hard on the way out: the reason to get down does not wait for it to arrive.
		if (CVarAIDuckByReason.GetValueOnAnyThread() != 0 && Data.NPC->GetRecentDamage() >= Data.DuckDamageThreshold)
		{
			Data.DuckUntil = (Data.NPC->GetWorld() ? Data.NPC->GetWorld()->GetTimeSeconds() : 0.0f) + Data.DuckSecondsOnDamage;
			Data.bShiftAfterDuck = true;
			Data.NPC->ClearRecentDamage();
			EnterPhase(Data, EShooterPeekPhase::ToHide);
			Data.NPC->SayTactic(TEXT("HIT: down behind cover"));
			NoteDuckReaction(Data.NPC, TEXT("hit, stepping out"));
			TryMoveTo(Data, Spot.HideLocation);
			break;
		}

		const float ToPeekDistance = FVector::Dist2D(NPCLocation, Spot.PeekLocation);

		// The slide of a slide peek. It cannot start from a standstill (SlideMinStartSpeed), so it is
		// launched mid-run, the first frame the run-up sprint has the speed, while there is still
		// ground to slide over - the same launch-on-the-move ShooterPush uses. A run too short to ever
		// get there simply arrives running.
		if (Data.bPeekSlide && !Data.bPeekSlideStarted && ToPeekDistance > 120.0f)
		{
			if (UApexMovementComponent* const SlideApex = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement()))
			{
				SlideApex->StartSlideToPoint(Spot.PeekLocation, 120.0f);
				if (SlideApex->IsSliding())
				{
					Data.bPeekSlideStarted = true;
					SlideApex->StopSprint();
					UE_LOG(LogTemp, Verbose, TEXT("[PEEK_DEBUG] %s slides into P (%.0f to go)"),
						*GetNameSafe(Data.NPC), ToPeekDistance);
				}
			}
		}

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
			// Over low cover the NPC fights from behind the wall it is standing at: no strafe, a
			// sideways leg would walk it off the cover.
			if (!Spot.bPeekOver)
			{
				StartStrafeLeg(Data, Spot.PeekLocation);
			}
		}
		else if (IsMoveStalled(Data, DeltaTime, ToPeekDistance))
		{
			// Could not step out. Back behind cover rather than standing in the open half way.
			UE_LOG(LogTemp, Verbose, TEXT("[PEEK_DEBUG] %s STALLED stepping out to P, %.0f left (best %.0f, arrive<=%.0f)"),
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
		//
		// Not in the first 0.3s: the pose of the peek is still arriving then. Standing up over a low
		// wall, or getting up at the end of a slide, takes the capsule and the gun a few frames, and a
		// muzzle still below the wall on the first tick would condemn a perfectly good corner.
		UApexMovementComponent* const PeekApex = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement());
		if (Data.bPeekSlideStarted && PeekApex && !PeekApex->IsSliding())
		{
			// The slide into P just ended (EndSlide stood the NPC up): now take the rolled pose.
			Data.bPeekSlideStarted = false;
			if (Data.bPeekCrouch)
			{
				PeekApex->StartCrouching();
			}
		}
		const bool bPoseSettled = Data.PhaseElapsed >= 0.3f && !(PeekApex && PeekApex->IsSliding());
		if (bPoseSettled && !Opportunity && (!FireTarget || !Data.NPC->HasLineOfSightTo(FireTarget)))
		{
			// In plain view and out of reach of the gun: under the wall. Down, and it stays down until
			// he comes out from under it (the hide end asks the same question before it comes up).
			if (FireTarget && FireTarget == Data.Target
				&& IsInDeadGroundFrom(Data.NPC, NPCLocation - FVector(0.0f, 0.0f, Data.NPC->GetSimpleCollisionHalfHeight()), FireTarget, /*bAskTheGun*/ false))
			{
				Data.BlindPeeks = 0;
				Data.NPC->SayTactic(TEXT("DEAD GROUND: he is under my wall, staying down"));
				EnterPhase(Data, EShooterPeekPhase::ToHide);
				TryMoveTo(Data, Spot.HideLocation);
				break;
			}

			// He is not in view. Is the place he went down behind? Then this corner is doing its job:
			// the soldier stays out with the gun on that place, and its aim goes on settling. Going
			// back and taking another corner is for a corner that looks at a wall, not for an enemy
			// who ducked. Recorded 2026-10-11: a player crouched behind a low wall at 20 m, and the
			// garrison answered every time with "nobody in view, back" and, twice in a row, with
			// "this corner sees nobody: taking another". 17 to 33 corners thrown away a fight.
			if (FireTarget && CVarAIDuckByReason.GetValueOnAnyThread() != 0)
			{
				// Where it thinks he is: the place the side last saw him at.
				const FVector Place = KnownPlaceOf(Data.Controller, FireTarget);

				FCollisionQueryParams WatchParams(SCENE_QUERY_STAT(PeekWatch), false, Data.NPC);
				WatchParams.AddIgnoredActor(FireTarget);
				FHitResult WatchHit;
				const FVector Eyes = Data.NPC->GetPawnViewLocation();
				const FVector Over = Place + FVector(0.0f, 0.0f, 60.0f);
				const bool bBlocked = Data.NPC->GetWorld()->LineTraceSingleByChannel(WatchHit, Eyes, Over, ECC_Visibility, WatchParams);

				// Clear all the way, or stopped only by what he is hiding behind (within 2.5 m of him).
				if (!bBlocked || FVector::Dist(WatchHit.ImpactPoint, Over) <= 250.0f)
				{
					if (UAIAccuracyComponent* const WatchAccuracy = Data.NPC->FindComponentByClass<UAIAccuracyComponent>())
					{
						WatchAccuracy->NoteAiming(FireTarget);
					}
					Data.BlindPeeks = 0;

					// The reasons to get down still apply while it watches.
					const UAIAccuracyComponent* const WatchPinned = Data.NPC->FindComponentByClass<UAIAccuracyComponent>();
					if (!Data.NPC->IsReloadingWeapon() && !(WatchPinned && WatchPinned->IsPinnedDown())
						&& Data.NPC->GetRecentDamage() < Data.DuckDamageThreshold)
					{
						// A gun that is held on its target before the shot (a scope) does not fire at the
						// place he went down behind: a round with no hold in front of it is the shot with no
						// warning. It watches, and the hold starts when he shows.
						const AShooterWeapon* const WatchGun = Data.NPC->GetWeapon();
						const bool bHoldsAim = WatchGun && WatchGun->HasAIAimHold();
						if (Data.PhaseElapsed < Data.WatchFireSeconds)
						{
							Data.bWatching = !bHoldsAim;
							Data.WatchPlace = Over;
							Data.NPC->SayTactic(TEXT("WATCHING the place he went down"));
							break;
						}

						// Long enough stood out: down for a moment, and up again.
						Data.DuckUntil = (Data.NPC->GetWorld() ? Data.NPC->GetWorld()->GetTimeSeconds() : 0.0f) + Data.WatchRestSeconds;
						Data.NPC->SayTactic(TEXT("WATCHED long enough: down for a moment"));
						EnterPhase(Data, EShooterPeekPhase::ToHide);
						TryMoveTo(Data, Spot.HideLocation);
						break;
					}

					// Hit or pinned while it watched: down for as long as the reason says, the same as
					// out at the peek point with the target in view. It used to fall through to "nobody
					// in view, back", which sets no time to stay down, and it was up again in 0.4 s.
					// Recorded 2026-10-11: 20 of the player's 102 hits in four fights landed on one
					// that was watching.
					if (!Data.NPC->IsReloadingWeapon())
					{
						const bool bWatchPinned = WatchPinned && WatchPinned->IsPinnedDown();
						Data.DuckUntil = (Data.NPC->GetWorld() ? Data.NPC->GetWorld()->GetTimeSeconds() : 0.0f)
							+ (bWatchPinned ? Data.DuckSecondsOnSuppression : Data.DuckSecondsOnDamage);
						Data.bShiftAfterDuck = true;
						Data.NPC->ClearRecentDamage();
						EnterPhase(Data, EShooterPeekPhase::ToHide);
						Data.NPC->SayTactic(bWatchPinned ? TEXT("PINNED: down behind cover") : TEXT("HIT: down behind cover"));
						NoteDuckReaction(Data.NPC, bWatchPinned ? TEXT("pinned, watching") : TEXT("hit, watching"));
						TryMoveTo(Data, Spot.HideLocation);
						break;
					}
				}
			}

			UE_LOG(LogTemp, Verbose, TEXT("[PEEK_DEBUG] %s corner is BLIND from P"), *GetNameSafe(Data.NPC));

			// Out at P with nobody to shoot at: back behind the corner. Twice in a row from the same
			// corner and the corner is no good (a corner is for shooting from): it is given up and
			// another one looked for, which is the ordinary search and so is not a move forward. A squad
			// on the defensive keeps what it has, blind or not.
			++Data.BlindPeeks;
			AAICombatCoordinator* const BlindCoordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
			// One that holds a ground (a garrison) is the exception to the exception: its next corner is
			// inside the same yard, so giving a blind one up costs it nothing and gets a gun back into
			// the fight. Recorded 2026-10-11: three of five stood behind corners that saw nobody.
			const bool bDefending = BlindCoordinator && BlindCoordinator->GetPostureFor(Data.NPC) == ESquadPosture::Defense
				&& !Data.NPC->HasHoldArea();

			if (!bDefending && !Data.bBoundPending && Data.BlindPeeks >= 2)
			{
				Data.BlindPeeks = 0;
				if (++Data.BlindCorners >= 2 && Data.NPC->CanWidenHoldArea())
				{
					Data.NPC->WidenHoldArea();
					Data.NPC->SayTactic(TEXT("HOLD: my ground sees nobody, taking the whole place"));
				}
				Data.NPC->SayTactic(TEXT("THIS CORNER SEES NOBODY: taking another"));
				Finder->ReleaseCover();
				Data.Controller->StopMovement();
				EnterPhase(Data, EShooterPeekPhase::Seeking);
				break;
			}

			Data.NPC->SayTactic(bDefending ? TEXT("DEFENDING: nobody in view, holding the corner") : TEXT("NOBODY IN VIEW, back behind the corner"));
			EnterPhase(Data, EShooterPeekPhase::ToHide);
			TryMoveTo(Data, Spot.HideLocation);
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
			Data.NPC->SayTactic(TEXT("COVERING a runner"));
			Data.LegElapsed += DeltaTime;
			if (Data.LegElapsed >= Data.LegDuration && !Spot.bPeekOver)
			{
				Data.LegSign = -Data.LegSign;
				StartStrafeLeg(Data, Spot.PeekLocation);
			}
			break;
		}

		// Same as on the way out: dry in the open means back behind the corner now, not when the
		// peek's clock says so. And it does not wait to BE dry: with the last rounds of the magazine it
		// is already on its way back, so the empty click happens behind the wall.
		if (Data.NPC->IsReloadingWeapon()
			|| (Data.PhaseElapsed > 0.5f && Data.NPC->GetMagazineFraction() < Data.LowMagazineRetreatBelow))
		{
			Data.NPC->SayTactic(TEXT("RELOADING, back to cover"));
			EnterPhase(Data, EShooterPeekPhase::ToHide);
			TryMoveTo(Data, Spot.HideLocation);
			break;
		}

		// Down for a reason [author, 2026-10-11]. It stays out and shoots until a suppressing round
		// comes past or it has been hit hard enough lately; the reload is the block above. The clock
		// is the old rule, kept behind the switch for comparing the two.
		if (CVarAIDuckByReason.GetValueOnAnyThread() != 0)
		{
			const UAIAccuracyComponent* const PeekAccuracy = Data.NPC->FindComponentByClass<UAIAccuracyComponent>();
			const bool bPinned = PeekAccuracy && PeekAccuracy->IsPinnedDown();
			const bool bHurt = Data.NPC->GetRecentDamage() >= Data.DuckDamageThreshold;
			if (bPinned || bHurt)
			{
				const float PeekNow = Data.NPC->GetWorld() ? Data.NPC->GetWorld()->GetTimeSeconds() : 0.0f;
				Data.DuckUntil = PeekNow + (bPinned ? Data.DuckSecondsOnSuppression : Data.DuckSecondsOnDamage);
				Data.bShiftAfterDuck = true;
				Data.NPC->ClearRecentDamage();
				EnterPhase(Data, EShooterPeekPhase::ToHide);
				Data.NPC->SayTactic(bPinned ? TEXT("PINNED: down behind cover") : TEXT("HIT: down behind cover"));
				NoteDuckReaction(Data.NPC, bPinned ? TEXT("pinned, out shooting") : TEXT("hit, out shooting"));
				TryMoveTo(Data, Spot.HideLocation);
				break;
			}
		}
		else if (Data.PhaseElapsed >= Data.PeekDuration)
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
		if (Data.LegElapsed >= Data.LegDuration && !Spot.bPeekOver)
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
	case EShooterPeekPhase::Seeking:
		// Silent on a RUN even though it is exposed: the runner is sprinting with its body on its
		// own direction of travel, so it has nothing to aim with. That silence is precisely what the
		// covering fire is buying.
		//
		// Everything else on the way to a corner, and standing with no corner at all, shoots
		// [author, 2026-10-10]: seen with a ready weapon and not firing is the one thing an enemy has
		// no reason to do. The walk to a FRESH corner used to be silent, which on an arena with little
		// cover was most of the fight. The line-of-sight test below keeps it from shooting walls.
		bWantsFire = !Data.bIsRelocating;
		break;

	default:
		// AtHide, and the same rule: behind a corner that hides it there is no line and so no fire,
		// which is the whole point of having one. A "corner" the target can see is not one (the search
		// hands out the best spot there is, and on open ground the best is still in view), and an
		// enemy standing in view with a ready gun and holding its fire is the one thing it has no
		// reason to do.
		bWantsFire = true;
		break;
	}

	const UEnemyCombatProfile* const FireProfile = Data.NPC->GetCombatProfile();

	// Remembered whenever it is true, so the blind-fire branch below has somewhere to aim. Kept even
	// after the line is lost, which is the entire value of it.
	const bool bSeesFireTarget = FireTarget && Data.NPC->HasLineOfSightTo(FireTarget);
	const float FireNow = Data.NPC->GetWorld() ? Data.NPC->GetWorld()->GetTimeSeconds() : 0.0f;
	if (bSeesFireTarget)
	{
		Data.BlindPeeks = 0;
		Data.BlindCorners = 0;
		Data.LastSeenTargetLocation = FireTarget->GetActorLocation();
		Data.bHasLastSeen = true;
		Data.LastSeenTime = FireNow;
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
	// And for anybody at all, for WatchFireSeconds after he went down, while the place he went down
	// behind is in view (bWatching): rounds go on arriving where he is going to come up.
	// Not while its head is turned to a shot or a shout from another side: the gun goes with the head.
	const AShooterAIController* const FireBrain = Cast<AShooterAIController>(Data.Controller);
	const bool bWatchFire = Data.bWatching && !(FireBrain && FireBrain->IsLookingAround());
	Data.bWatching = false;
	const bool bBlindFire = bWantsFire && !bSeesFireTarget
		&& Data.Phase == EShooterPeekPhase::AtPeek
		&& (bWatchFire || (Data.bHasLastSeen && FireProfile && FireProfile->bFireAtLastSeenWhenBlind));

	if (bBlindFire)
	{
		// Watching: the place the SIDE last knew him at, which is newer than what this one saw itself.
		Data.Controller->SetFocalPoint(bWatchFire ? Data.WatchPlace : Data.LastSeenTargetLocation);
	}

	// The one it is fighting is out of sight and somebody else is not: that one.
	AActor* ShootAt = FireTarget;
	bool bSeesShootAt = bSeesFireTarget;
	if (!bSeesFireTarget && !bBlindFire && Opportunity && Data.NPC->HasLineOfSightTo(Opportunity))
	{
		ShootAt = Opportunity;
		bSeesShootAt = true;
		Data.Controller->SetFocus(Opportunity);
		Data.NPC->SayTactic(TEXT("TARGET OF OPPORTUNITY: shooting who shows"));
	}

	// A change of target closes the request; the next tick opens it on the new one.
	if (Data.bIsShooting && ShootAt && Data.ShootingAt.Get() != ShootAt)
	{
		StopShooting(Data);
	}

	if (bWantsFire && ShootAt && (bSeesShootAt || bBlindFire))
	{
		if (Data.Phase == EShooterPeekPhase::AtHide && !Data.bPeekedSinceCover)
		{
			// Shooting from the hide end means the hide end is in view: this corner has been traded
			// from, so the exposure recheck is free to throw it away.
			Data.bPeekedSinceCover = true;
			Data.NPC->SayTactic(TEXT("SEEN in cover, shooting"));
		}

		if (!Data.bIsShooting)
		{
			StartShooting(Data, ShootAt);
			Data.ShootingAt = ShootAt;
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

	UE_LOG(LogTemp, Verbose, TEXT("[PEEK_DEBUG] %s EXIT Peek from phase %s"),
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
	Data.bMoveIssued = Result.Code == EPathFollowingRequestResult::RequestSuccessful;

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

	FVector ToTarget = (KnownPlaceOf(Data.Controller, Data.Target) - Origin).GetSafeNormal2D();
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
	UE_LOG(LogTemp, Verbose, TEXT("[PEEK_DEBUG] %s phase %s -> %s (spent %.2fs)"),
		*GetNameSafe(Data.NPC), PeekPhaseName(Data.Phase), PeekPhaseName(NewPhase), Data.PhaseElapsed);

	Data.Phase = NewPhase;
	Data.PhaseElapsed = 0.0f;

	if (NewPhase == EShooterPeekPhase::ToHide)
	{
		Data.bRunDecided = false;
		Data.bEntrySlideStarted = false;
	}

	if (Data.NPC)
	{
		switch (NewPhase)
		{
		case EShooterPeekPhase::Seeking: Data.NPC->SayTactic(TEXT("LOOKING for cover")); break;
		case EShooterPeekPhase::ToHide:  Data.NPC->SayTactic(Data.bPeekedSinceCover ? TEXT("BACK to cover") : TEXT("TO COVER (walk, gun up)")); break;
		case EShooterPeekPhase::AtHide:  Data.NPC->SayTactic(TEXT("IN COVER")); break;
		case EShooterPeekPhase::ToPeek:  Data.NPC->SayTactic(TEXT("STEPPING OUT")); break;
		case EShooterPeekPhase::AtPeek:  Data.NPC->SayTactic(TEXT("OUT, shooting")); break;
		default: break;
		}
	}

	// The pose of the cycle. Here rather than at the movement calls, because this is the one funnel
	// every phase change goes through regardless of which branch asked for it.
	//   AtHide  - down behind LOW cover (it hides a crouched body only), otherwise standing.
	//   ToPeek  - this peek's rolls (profile chances): a slide run-up sprints, a crouch peek goes down
	//             for the step out; over low cover neither, the peek is standing up where it is.
	//   AtPeek  - over low cover: stand, that IS the peek.
	//   ToHide / Seeking - up, and any run-up sprint off.
	if (Data.NPC)
	{
		UApexMovementComponent* const Apex = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement());
		const UCoverFinderComponent* const StanceFinder = Data.NPC->FindComponentByClass<UCoverFinderComponent>();
		const bool bLow = StanceFinder && StanceFinder->HasCover() && StanceFinder->GetCover().bLowCover;
		const bool bOver = StanceFinder && StanceFinder->HasCover() && StanceFinder->GetCover().bPeekOver;

		if (Apex)
		{
			switch (NewPhase)
			{
			case EShooterPeekPhase::AtHide:
				if (bLow)
				{
					Apex->StartCrouching();
				}
				break;

			case EShooterPeekPhase::ToPeek:
			{
				const UEnemyCombatProfile* const StanceProfile = Data.NPC->GetCombatProfile();
				Data.bPeekCrouch = !bOver && StanceProfile && StanceProfile->RollCrouchPeek();
				// The slide out of cover is switched off [author, 2026-10-11]: a slide is how an enemy gets
				// INTO cover now (SlideEntryDistance), and sliding out to shoot read as a cartoon. The
				// profile's SlidePeekChance stays where it is for the day it comes back.
				Data.bPeekSlide = false;
				Data.bPeekSlideStarted = false;
				if (Data.bPeekSlide)
				{
					// Up and running: the slide needs the speed (SlideMinStartSpeed), see ToPeek.
					if (Apex->IsCrouching())
					{
						Apex->StopCrouching();
					}
					Apex->StartSprint();
				}
				else if (Data.bPeekCrouch)
				{
					Apex->StartCrouching();
				}
				else if (!bOver && Apex->IsCrouching())
				{
					Apex->StopCrouching();
				}
				UE_LOG(LogTemp, Verbose, TEXT("[PEEK_DEBUG] %s peek rolls: low=%d over=%d crouch=%d slide=%d"),
					*GetNameSafe(Data.NPC), bLow, bOver, Data.bPeekCrouch, Data.bPeekSlide);
				break;
			}

			case EShooterPeekPhase::AtPeek:
				if (Data.bPeekSlide && Apex->IsSprinting())
				{
					Apex->StopSprint();
				}
				if (bOver && Apex->IsCrouching())
				{
					Apex->StopCrouching();
				}
				break;

			case EShooterPeekPhase::ToHide:
			case EShooterPeekPhase::Seeking:
				if (Apex->IsCrouching() && !(bOver && NewPhase == EShooterPeekPhase::ToHide))
				{
					Apex->StopCrouching();
				}
				if (Data.bPeekSlide && Apex->IsSprinting())
				{
					Apex->StopSprint();
				}
				Data.bPeekSlide = false;
				Data.bPeekSlideStarted = false;
				break;

			default:
				break;
			}
		}
	}

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

	// Who took the corner away. Asking the cover component per candidate rather than assuming it was
	// the current target: in coop the one who flanked is usually not the one being shot at, and
	// naming the wrong one would point the covering fire at the wrong place. Candidates are everyone
	// hostile to this NPC, so a corner opened by another faction is answered the same way.
	APawn* Opener = nullptr;
	if (const UCoverFinderComponent* const Finder = Data.NPC->FindComponentByClass<UCoverFinderComponent>())
	{
		TArray<APawn*> Hostiles;
		PolarityTeams::GatherHostilePawns(Data.NPC, Hostiles);

		float WorstThreat = -1.0f;
		for (APawn* const Hostile : Hostiles)
		{
			if (!Finder->IsCoverOpenedBy(Hostile))
			{
				continue;
			}

			const float Threat = Coordinator->GetThreatFor(Hostile);
			if (Threat > WorstThreat)
			{
				WorstThreat = Threat;
				Opener = Hostile;
			}
		}
	}

	// A bound leaves a corner nobody opened, so there is nobody to name. The one to sit on is then
	// simply the one this NPC is fighting: that is who will see it run.
	if (!Opener && Data.bBoundPending)
	{
		Opener = Cast<APawn>(Data.Target.Get());
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
	//
	// What the coordinator does decide is whether this fire is AT the target or near it. Asked at the
	// start of every string (the task closes and reopens the request around each burst cooldown), so
	// the aimed slots rotate through the squad at the rhythm of the bursts.
	AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC);
	const bool bAimed = !Coordinator || Coordinator->RequestAimedFire(Data.NPC);
	if (UAIAccuracyComponent* const Accuracy = Data.NPC->FindComponentByClass<UAIAccuracyComponent>())
	{
		Accuracy->SetPinFire(!bAimed);
	}

	Data.NPC->StartShooting(ShootAt, /*bHasExternalPermission*/ true);
	Data.bIsShooting = true;
}

void FSTTask_ShooterPeek::StopShooting(FInstanceDataType& Data) const
{
	if (Data.NPC)
	{
		Data.NPC->StopShooting();

		if (AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(Data.NPC))
		{
			Coordinator->ReleaseAimedFire(Data.NPC);
		}
		if (UAIAccuracyComponent* const Accuracy = Data.NPC->FindComponentByClass<UAIAccuracyComponent>())
		{
			Accuracy->SetPinFire(false);
		}
	}
	Data.bIsShooting = false;
}

void FSTTask_ShooterPeek::ReleaseAll(FInstanceDataType& Data) const
{
	StopShooting(Data);

	// The crouch belongs to the peek (and to low cover), and so does the slide run-up sprint: an exit
	// through any other door (tree transition, knockback, death) must not leave the NPC walking around
	// at crouch height or with its sprint latched (a latched sprint drops the rifle arms, see the
	// turret-cover gotcha).
	if (Data.NPC)
	{
		if (UApexMovementComponent* const Apex = Cast<UApexMovementComponent>(Data.NPC->GetCharacterMovement()))
		{
			if (Apex->IsCrouching())
			{
				Apex->StopCrouching();
			}
			if (Data.bPeekSlide && Apex->IsSprinting())
			{
				Apex->StopSprint();
			}
		}
		Data.bPeekSlide = false;
		Data.bPeekSlideStarted = false;
	}

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

		// Only this task's own movement, for the same reason as the push above.
		if (Data.bMoveIssued)
		{
			Data.Controller->StopMovement();
		}
	}
	Data.bMoveIssued = false;
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

// ============================================================================
// TakeStance
// ============================================================================

EStateTreeRunStatus FSTTask_TakeStance::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	Data.bAtStance = false;
	Data.bUnreachable = false;
	Data.TimeSinceMoveIssued = 0.0f;

	if (!Data.Controller)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (Data.bHoldFire)
	{
		// hold-fire is its own verb, and this is the whole of it on the way in: an ambush that keeps
		// shooting from the previous state is not an ambush.
		if (AShooterNPC* const NPC = Cast<AShooterNPC>(Data.Controller->GetPawn()))
		{
			NPC->StopShooting();
		}
	}

	// Already standing in it. Not a failure and not a move: an ambush point chosen where the NPC
	// happens to be is a perfectly good ambush point.
	const APawn* const Pawn = Data.Controller->GetPawn();
	if (Pawn && FVector::Dist2D(Pawn->GetActorLocation(), Data.StanceLocation) <= Data.AcceptanceRadius)
	{
		Data.bAtStance = true;
		Data.Controller->StopMovement();
		ApplyFacing(Data);
		return EStateTreeRunStatus::Running;
	}

	FAIMoveRequest MoveRequest;
	MoveRequest.SetGoalLocation(Data.StanceLocation);
	MoveRequest.SetAcceptanceRadius(Data.AcceptanceRadius);
	MoveRequest.SetUsePathfinding(true);
	MoveRequest.SetAllowPartialPath(true);
	MoveRequest.SetProjectGoalLocation(true);

	const FPathFollowingRequestResult Result = Data.Controller->MoveTo(MoveRequest);

	if (Result.Code == EPathFollowingRequestResult::Failed)
	{
		// Reported rather than failed: the tree that picked this point is the thing that can pick
		// another one, and a failed task would drop the NPC out of the state that knows that.
		Data.bUnreachable = true;
		UE_LOG(LogTemp, Warning, TEXT("[AI_DEBUG] %s TakeStance: cannot reach %s"),
			*GetNameSafe(Data.Controller->GetPawn()), *Data.StanceLocation.ToCompactString());
		return EStateTreeRunStatus::Running;
	}

	if (Result.Code == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		Data.bAtStance = true;
		ApplyFacing(Data);
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_TakeStance::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Controller)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (Data.bHoldFire)
	{
		if (AShooterNPC* const NPC = Cast<AShooterNPC>(Data.Controller->GetPawn()))
		{
			NPC->StopShooting();
		}
	}

	// Holding: keep pointing where the stance wants, every tick. Set once and it drifts, because
	// anything else that touches focus or rotation mode wins by being later.
	if (Data.bAtStance)
	{
		ApplyFacing(Data);
		return EStateTreeRunStatus::Running;
	}

	Data.TimeSinceMoveIssued += DeltaTime;

	if (const UPathFollowingComponent* const PathComp = Data.Controller->GetPathFollowingComponent())
	{
		if (PathComp->DidMoveReachGoal())
		{
			Data.bAtStance = true;
			Data.Controller->StopMovement();
			ApplyFacing(Data);
			return EStateTreeRunStatus::Running;
		}

		// Idle without having arrived means the follower gave up. The grace period is for the frame
		// or two right after MoveTo, where Idle only means "has not started yet".
		if (PathComp->GetStatus() == EPathFollowingStatus::Idle && Data.TimeSinceMoveIssued > 0.5f)
		{
			Data.bUnreachable = true;
		}
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_TakeStance::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Controller)
	{
		Data.Controller->StopMovement();
		Data.Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

void FSTTask_TakeStance::ApplyFacing(const FInstanceDataType& Data) const
{
	if (!Data.Controller)
	{
		return;
	}

	if (IsValid(Data.FaceTarget))
	{
		Data.Controller->SetFocus(Data.FaceTarget);
		return;
	}

	if (Data.FacingDirection.IsNearlyZero())
	{
		return;
	}

	// A focal POINT rather than a rotation: the controller already knows how to turn a body towards
	// a point, and a stance that wants to look north is a stance looking at a spot to the north.
	const APawn* const Pawn = Data.Controller->GetPawn();
	if (!Pawn)
	{
		return;
	}

	const FVector Look = Pawn->GetActorLocation() + Data.FacingDirection.GetSafeNormal() * 1000.0f;
	Data.Controller->SetFocalPoint(Look);
}

#if WITH_EDITOR
FText FSTTask_TakeStance::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	if (Data && Data->bHoldFire)
	{
		return FText::FromString(TEXT("Take stance and hold fire (ambush)"));
	}

	return FText::FromString(TEXT("Take stance: move to point, hold it, face a direction"));
}
#endif

// ============================================================================
// SuppressiveFire
// ============================================================================

EStateTreeRunStatus FSTTask_SuppressiveFire::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.NPC)
	{
		return EStateTreeRunStatus::Failed;
	}

	UAIAccuracyComponent* const Accuracy = Data.NPC->FindComponentByClass<UAIAccuracyComponent>();
	if (!Accuracy)
	{
		// No accuracy component means no way to miss on purpose, and firing accurately while
		// pretending to suppress would quietly turn covering fire into an execution.
		UE_LOG(LogTemp, Warning, TEXT("[AI_DEBUG] %s SuppressiveFire: no UAIAccuracyComponent"),
			*GetNameSafe(Data.NPC));
		return EStateTreeRunStatus::Failed;
	}

	Accuracy->SetSuppressiveFire(true);
	return EStateTreeRunStatus::Running;
}

void FSTTask_SuppressiveFire::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	// Off on EVERY exit, including a failure or a state change nobody planned. Whoever switches the
	// mode on owns switching it off, and this is the only place that can promise it.
	if (Data.NPC)
	{
		if (UAIAccuracyComponent* const Accuracy = Data.NPC->FindComponentByClass<UAIAccuracyComponent>())
		{
			Accuracy->SetSuppressiveFire(false);
		}
	}
}

#if WITH_EDITOR
FText FSTTask_SuppressiveFire::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Suppressive fire: aim to miss close for as long as this state runs"));
}
#endif
