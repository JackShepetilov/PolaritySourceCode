// PolarityAITasks.h
// StateTree tasks for Polarity AI system

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "PolarityAITasks.generated.h"

class APawn;
class AActor;
class AShooterNPC;
class AAIController;

// ============================================================================
// RequestAttackPermission - Request permission from coordinator
// ============================================================================

USTRUCT()
struct FSTTask_RequestAttackPermission_Data
{
	GENERATED_BODY()

	/** The NPC requesting permission */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<APawn> NPC;

	/** Output: whether permission was granted */
	UPROPERTY(EditAnywhere, Category = "Output")
	bool bPermissionGranted = false;
};

USTRUCT(DisplayName = "Request Attack Permission", Category = "Polarity|AI")
struct FSTTask_RequestAttackPermission : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_RequestAttackPermission_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, 
		const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

// ============================================================================
// NotifyAttackComplete - Tell coordinator attack is done
// ============================================================================

USTRUCT()
struct FSTTask_NotifyAttackComplete_Data
{
	GENERATED_BODY()

	/** The NPC that finished attacking */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<APawn> NPC;
};

USTRUCT(DisplayName = "Notify Attack Complete", Category = "Polarity|AI")
struct FSTTask_NotifyAttackComplete : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_NotifyAttackComplete_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

// ============================================================================
// ExecuteRetreat - Move to retreat destination
// ============================================================================

USTRUCT()
struct FSTTask_ExecuteRetreat_Data
{
	GENERATED_BODY()

	/** The NPC to retreat */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<APawn> NPC;

	/** AI Controller */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	/** Acceptance radius for movement */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "10.0"))
	float AcceptanceRadius = 50.0f;
};

USTRUCT(DisplayName = "Execute Retreat", Category = "Polarity|AI")
struct FSTTask_ExecuteRetreat : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ExecuteRetreat_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

// ============================================================================
// ShootWithAccuracy - Fire at target using accuracy component
// ============================================================================

USTRUCT()
struct FSTTask_ShootWithAccuracy_Data
{
	GENERATED_BODY()

	/** The shooting NPC */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AShooterNPC> NPC;

	/** Target to shoot at */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	/** Duration to shoot (seconds, 0 = single shot) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0"))
	float ShootDuration = 0.0f;

	/** Internal timer */
	float ElapsedTime = 0.0f;
};

USTRUCT(DisplayName = "Shoot With Accuracy", Category = "Polarity|AI")
struct FSTTask_ShootWithAccuracy : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ShootWithAccuracy_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

// ============================================================================
// RegisterWithCoordinator - Register NPC on spawn
// ============================================================================

USTRUCT()
struct FSTTask_RegisterWithCoordinator_Data
{
	GENERATED_BODY()

	/** The NPC to register */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<APawn> NPC;
};

USTRUCT(DisplayName = "Register With Coordinator", Category = "Polarity|AI")
struct FSTTask_RegisterWithCoordinator : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_RegisterWithCoordinator_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

// ============================================================================
// TriggerRetreatFromDamage - Manually trigger retreat (call from damage event)
// ============================================================================

USTRUCT()
struct FSTTask_TriggerRetreat_Data
{
	GENERATED_BODY()

	/** The NPC to retreat */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<APawn> NPC;

	/** The attacker to retreat from */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Attacker;

	/** Output: whether retreat was triggered */
	UPROPERTY(EditAnywhere, Category = "Output")
	bool bRetreatTriggered = false;
};

USTRUCT(DisplayName = "Trigger Retreat", Category = "Polarity|AI")
struct FSTTask_TriggerRetreat : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_TriggerRetreat_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

// ============================================================================
// MoveWithStrafe - Move to location while keeping focus on target (strafing)
// ============================================================================

USTRUCT()
struct FSTTask_MoveWithStrafe_Data
{
	GENERATED_BODY()

	/** AI Controller for movement */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	/** Target to keep looking at while moving */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> FocusTarget;

	/** Destination to move to */
	UPROPERTY(EditAnywhere, Category = "Input")
	FVector Destination = FVector::ZeroVector;

	/** Acceptance radius for reaching destination */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "10.0"))
	float AcceptanceRadius = 100.0f;

	/** If true, uses pathfinding. If false, moves directly */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bUsePathfinding = true;
};

USTRUCT(DisplayName = "Move With Strafe", Category = "Polarity|AI")
struct FSTTask_MoveWithStrafe : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_MoveWithStrafe_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

// ============================================================================
// BurstFire - Fire a burst of shots at target (uses ShooterNPC burst system)
// ============================================================================

USTRUCT()
struct FSTTask_BurstFire_Data
{
	GENERATED_BODY()

	/** The ShooterNPC that will shoot */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AShooterNPC> NPC;

	/** Target to shoot at */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	/** If true, use combat coordinator for attack permission */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bUseCoordinator = true;

	// Runtime state
	bool bStartedShooting = false;
};

USTRUCT(DisplayName = "Burst Fire", Category = "Polarity|AI|Shooter")
struct FSTTask_BurstFire : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_BurstFire_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

// ============================================================================
// FlyAndShoot - Continuous flying movement while shooting when ready
// For FlyingDrone: picks random points around target, moves towards them,
// and fires bursts whenever off cooldown and has LOS
// ============================================================================

class AFlyingDrone;

USTRUCT()
struct FSTTask_FlyAndShoot_Data
{
	GENERATED_BODY()

	/** The FlyingDrone NPC */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;

	/** Target to orbit and shoot at */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	/** Horizontal radius for point selection around target */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "100.0"))
	float OrbitRadius = 800.0f;

	/** Minimum height offset for patrol points */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0"))
	float MinHeight = 200.0f;

	/** Maximum height offset for patrol points */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0"))
	float MaxHeight = 400.0f;

	/** Acceptance radius for reaching waypoint */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "10.0"))
	float AcceptanceRadius = 150.0f;

	/** If true, use combat coordinator for attack permission */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bUseCoordinator = true;

	// Runtime state
	FVector CurrentDestination = FVector::ZeroVector;
	bool bHasDestination = false;
	bool bIsShooting = false;

	/** Time when LOS was last confirmed (for repositioning when LOS lost too long) */
	float LastLOSTime = 0.0f;
};

/** How long without LOS before drone forces a reposition (seconds) */
static constexpr float FlyAndShoot_LOSLostRepositionTime = 1.5f;

USTRUCT(DisplayName = "Fly And Shoot", Category = "Polarity|AI|Drone")
struct FSTTask_FlyAndShoot : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_FlyAndShoot_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif

private:
	/** Pick a new random destination around target */
	bool PickNewDestination(FInstanceDataType& Data) const;

	/** Check if drone can shoot (not dead, off cooldown, has LOS, has permission) */
	bool CanShoot(const FInstanceDataType& Data) const;

	/** Start shooting at target */
	void StartShooting(FInstanceDataType& Data) const;

	/** Stop shooting */
	void StopShooting(FInstanceDataType& Data) const;
};

// ============================================================================
// RunAndShoot - Ground NPC strafing movement while shooting when ready
// For ShooterNPC: picks random nav points around target, moves via pathfinding
// while facing target (strafing), and fires bursts when off cooldown and has LOS
// ============================================================================

USTRUCT()
struct FSTTask_RunAndShoot_Data
{
	GENERATED_BODY()

	/** The ShooterNPC */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AShooterNPC> NPC;

	/** AI Controller for movement */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	/** Target to strafe around and shoot at */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	/** Maximum distance from target when selecting move points */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "100.0"))
	float MaxDistanceFromTarget = 1200.0f;

	/** Minimum distance from target when selecting move points */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0"))
	float MinDistanceFromTarget = 400.0f;

	/** Acceptance radius for reaching waypoint */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "10.0"))
	float AcceptanceRadius = 100.0f;

	/** If true, use combat coordinator for attack permission */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bUseCoordinator = true;

	// Runtime state
	FVector CurrentDestination = FVector::ZeroVector;
	bool bHasDestination = false;
	bool bIsShooting = false;

	/** Time when LOS was last confirmed (for repositioning when LOS lost too long) */
	float LastLOSTime = 0.0f;

	/** Stuck detection: position at last movement check */
	FVector LastStuckCheckPosition = FVector::ZeroVector;
	/** Stuck detection: time of last movement check */
	float LastStuckCheckTime = 0.0f;
};

/** How long without LOS before NPC forces a reposition (seconds) */
static constexpr float RunAndShoot_LOSLostRepositionTime = 2.0f;

/** How long before stuck detection triggers (seconds) */
static constexpr float RunAndShoot_StuckCheckInterval = 2.5f;
/** Minimum distance NPC must move within the interval to not be considered stuck (cm) */
static constexpr float RunAndShoot_StuckDistanceThreshold = 30.0f;

USTRUCT(DisplayName = "Run And Shoot", Category = "Polarity|AI|Shooter")
struct FSTTask_RunAndShoot : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_RunAndShoot_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif

private:
	/** Pick a new random destination around target using NavMesh */
	bool PickNewDestination(FInstanceDataType& Data) const;

	/** Check if NPC can shoot (not dead, off cooldown, has LOS, has permission) */
	bool CanShoot(const FInstanceDataType& Data) const;

	/** Start shooting at target */
	void StartShooting(FInstanceDataType& Data) const;

	/** Stop shooting */
	void StopShooting(FInstanceDataType& Data) const;
};

// ============================================================================
// GetRandomNavPoint - Get a random navigable point around the NPC
// ============================================================================

USTRUCT()
struct FSTTask_GetRandomNavPoint_Data
{
	GENERATED_BODY()

	/** The pawn to find point around */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<APawn> Pawn;

	/** Optional: Target to stay within range of */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	/** Radius to search for random point */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "100.0"))
	float SearchRadius = 500.0f;

	/** If Target is set, stay within this distance of target */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "100.0"))
	float MaxDistanceFromTarget = 1500.0f;

	/** If Target is set, stay at least this far from target */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0"))
	float MinDistanceFromTarget = 300.0f;

	/** Output: The random point found */
	UPROPERTY(EditAnywhere, Category = "Output")
	FVector RandomPoint = FVector::ZeroVector;

	/** Output: Whether a valid point was found */
	UPROPERTY(EditAnywhere, Category = "Output")
	bool bFoundPoint = false;
};

USTRUCT(DisplayName = "Get Random Nav Point", Category = "Polarity|AI")
struct FSTTask_GetRandomNavPoint : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_GetRandomNavPoint_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

// ============================================================================
// ShooterPush - how a shielded enemy closes on its target.
//
// Replaces RunAndShoot, which despite the name was a STOP-and-shoot: the moment it was allowed to
// fire it called StopMovement, waited for its own velocity to fall below a threshold and only then
// opened up, and it did not reposition at all while firing. Movement and fire were mutually
// exclusive by construction, so eight enemies arranged themselves on a ring around the player and
// took turns shooting from a standstill.
//
// The shape here is different in one decision that everything else follows from: THE PHASE IS A
// FUNCTION OF DISTANCE, recomputed every tick, not a sequence with memory. So a player who charges
// an enemy finds it already fighting at the range they created, with no code anywhere that skips a
// phase, and the same is true running backwards.
//
//   Approach  (> SprintDistance)                 diagonal walk, firing
//   Sprint    (DuelDistance .. SprintDistance)   diagonal sprint, silent
//   Duel      (<= DuelDistance)                  lateral strafe, firing
//   Withdraw  (mode, not a band)                 reverse of Approach, occasional fire
//
// The slide is not a phase; it is an event on the Sprint -> Duel boundary, and it is allowed to
// simply not happen. UApexMovementComponent::CanSlide already requires ground, no cooldown and
// SlideMinStartSpeed, so an enemy that never got up to speed walks into the duel instead. No branch
// here needs to know about that.
// ============================================================================

/** Which band of the fight this NPC is in. Derived, never stored as intent. */
UENUM()
enum class EShooterPushPhase : uint8
{
	Approach,
	Sprint,
	Duel,
	Withdraw
};

USTRUCT()
struct FSTTask_ShooterPush_Data
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AShooterNPC> NPC;

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	// ---- Distance bands ----

	/** Beyond this the NPC is approaching on foot, firing. */
	UPROPERTY(EditAnywhere, Category = "Distances", meta = (ClampMin = "200.0"))
	float SprintDistance = 1200.0f;

	/** Inside this the NPC stops closing and duels. */
	UPROPERTY(EditAnywhere, Category = "Distances", meta = (ClampMin = "100.0"))
	float DuelDistance = 600.0f;

	/** How far it backs off to before handing the push to somebody else. */
	UPROPERTY(EditAnywhere, Category = "Distances", meta = (ClampMin = "200.0"))
	float WithdrawDistance = 1600.0f;

	// ---- Movement shape ----

	/** How far off the straight line each leg runs. Zero would be a beeline, which reads as a zombie;
	 *  ninety would be a circle and never arrive. */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0", ClampMax = "80.0"))
	float DiagonalAngleDeg = 35.0f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.1"))
	float LegDurationMin = 0.8f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.1"))
	float LegDurationMax = 1.2f;

	/** Lateral hold in the duel. This is the firing-range dummy: no closing, just left and right. */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.1"))
	float StrafeHoldMin = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.1"))
	float StrafeHoldMax = 1.6f;

	// ---- Rotation ----

	/** How long it stays in the duel before withdrawing and letting somebody else come in. An enemy
	 *  that never leaves your face reads as oppression; one that rotates gives the fight a rhythm the
	 *  player can learn and use. */
	UPROPERTY(EditAnywhere, Category = "Rotation", meta = (ClampMin = "0.5"))
	float DuelDuration = 6.0f;

	/** Chance per firing opportunity that a withdrawing NPC actually shoots. Withdrawal is meant to
	 *  read as leaving, not as fighting backwards. */
	UPROPERTY(EditAnywhere, Category = "Rotation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WithdrawFireChance = 0.35f;

	// ---- Sprint commitment ----

	/** How long before arrival the charge commits to the slide. Expressed as time rather than
	 *  distance so it survives retuning the sprint speed: the trigger distance is this times the
	 *  speed the NPC is actually running at. */
	UPROPERTY(EditAnywhere, Category = "Sprint", meta = (ClampMin = "0.0"))
	float SlideLeadTime = 0.45f;

	/** Lower bound on how often the sprint goal may be re-issued while the target moves. One path
	 *  request per pusher per this interval; the goal itself moves continuously regardless. */
	UPROPERTY(EditAnywhere, Category = "Sprint", meta = (ClampMin = "0.05"))
	float SprintRetargetInterval = 0.25f;

	/** How far the goal has to drift before re-issuing is worth a path request. Below this the old
	 *  path is still close enough, and re-requesting only makes the run stutter. */
	UPROPERTY(EditAnywhere, Category = "Sprint", meta = (ClampMin = "0.0"))
	float SprintRetargetTolerance = 100.0f;

	/** How fast the slide is allowed to steer toward its committed point, in degrees per second.
	 *  This is what keeps a moving target from turning the slide into a snap. */
	UPROPERTY(EditAnywhere, Category = "Sprint", meta = (ClampMin = "0.0"))
	float SlideSteerRateDeg = 120.0f;

	// ---- Last stand ----
	//
	// The cornered variant is this same task with three knobs turned, not a second task. Everything
	// about the movement is already here, and a duplicate would be a second copy of the slide to
	// keep in step with this one.

	/** Never hand the push over. The duel clock stops setting the withdrawal, so this NPC closes and
	 *  stays closed: no rotation out, no letting somebody else take the front. */
	UPROPERTY(EditAnywhere, Category = "Last Stand")
	bool bNeverWithdraw = false;

	/** Fire through the sprint as well. Normally the charge is silent, which is what makes it read
	 *  as a charge; one that shoots the whole way reads as somebody who has stopped budgeting. */
	UPROPERTY(EditAnywhere, Category = "Last Stand")
	bool bFireWhileSprinting = false;

	/** Degrees per second the committed bearing travels WHILE THE SLIDE IS RUNNING.
	 *
	 *  The bearing is an angle around the target, so advancing it walks the slide's target point
	 *  around the player, and the slide follows it into an arc: the NPC skids past and around
	 *  rather than into. Zero is the ordinary charge, which commits to one spot and brakes onto it.
	 *
	 *  This cannot make the movement jitter no matter how large it is. The point is only an input:
	 *  UApexMovementComponent steers SlideDirection toward it at no more than SlideSteerRateDeg per
	 *  second and drives Velocity from that smoothed direction, so the visible path is rate limited
	 *  by construction.
	 *
	 *  Note the arc does not converge, so the slide ends on its duration ceiling or its minimum
	 *  speed rather than on arrival. That is the intended shape here. */
	UPROPERTY(EditAnywhere, Category = "Last Stand", meta = (ClampMin = "0.0"))
	float SlideOrbitRateDeg = 0.0f;

	/** The charge-and-orbit above needs room to actually happen in: a slide that skids into the
	 *  nearest wall is not the desperate lunge it is supposed to read as. Checked ONCE on entering
	 *  Last Stand, around the TARGET (this is where the orbit happens, not where the NPC currently
	 *  stands): a handful of traces radiate out to this distance, and the fraction that come back
	 *  clear decides whether the arena has space for it (design decision, author's call - a tight
	 *  corner should not get the same finishing move as an open room). Failing the check does not
	 *  cancel Last Stand, it changes what it looks like: see bOrbitOpen below EnterState. */
	UPROPERTY(EditAnywhere, Category = "Last Stand", meta = (ClampMin = "0.0"))
	float OrbitOpenRadius = 500.0f;

	/** How much of the ring around the target has to come back clear for OrbitOpenRadius's check to
	 *  call the area open. 0.5 = half the sampled directions. Lower admits tighter rooms into the
	 *  charge-and-orbit; 1.0 demands a fully open circle. */
	UPROPERTY(EditAnywhere, Category = "Last Stand", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OrbitOpenFraction = 0.5f;

	// ---- Runtime ----

	EShooterPushPhase Phase = EShooterPushPhase::Approach;
	EShooterPushPhase PreviousPhase = EShooterPushPhase::Approach;

	/** Which side the current leg is angled to. Flips at the end of every leg. */
	float LegSign = 1.0f;

	/** World bearing, in degrees, of the spot on the duel ring this charge committed to - measured
	 *  from the TARGET outwards, which is the whole point: the commitment is an angle around the
	 *  player, not a place on the map. While the player moves, the spot travels with them and the
	 *  run bends gradually to follow instead of picking a fresh point and snapping to it. */
	float CommittedBearingDeg = 0.0f;
	bool bHasCommittedBearing = false;

	/** Throttle for re-issuing the sprint goal as that spot moves. */
	float LastRetargetTime = 0.0f;

	/** The charge reached its committed spot. This, and not the distance test, is what opens the
	 *  duel: the slide stops a little short of the ring by design (arrival tolerance), so waiting
	 *  for distance to fall under DuelDistance meant standing up and walking the last few
	 *  centimetres before the duel would start. Arriving IS the end of the charge. */
	bool bChargeArrived = false;

	/** Edge detector for the above: true while the braking slide is running. */
	bool bWasSlidingToPoint = false;

	float LegEndTime = 0.0f;
	FVector LegDestination = FVector::ZeroVector;
	bool bHasLeg = false;

	/** World time the duel started, for the rotation clock. */
	float DuelEnteredTime = 0.0f;

	/** True once the duel clock ran out, until the NPC is far enough away again. */
	bool bWithdrawing = false;

	/** Whether the last firing opportunity during withdrawal came up heads. Rolled once per leg so
	 *  the NPC does not flicker between firing and not within a single leg. */
	bool bWithdrawLegFires = false;

	bool bIsShooting = false;

	/** Stuck detection, same idea as the task this replaces. */
	FVector LastStuckCheckPosition = FVector::ZeroVector;
	float LastStuckCheckTime = 0.0f;
};

/** Seconds without progress before a leg is abandoned. */
static constexpr float ShooterPush_StuckCheckInterval = 1.5f;
static constexpr float ShooterPush_StuckDistanceThreshold = 25.0f;

USTRUCT(DisplayName = "Shooter Push", Category = "Polarity|AI|Shooter")
struct FSTTask_ShooterPush : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ShooterPush_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif

private:

	/** Distance band, plus the withdrawal mode which overrides it. */
	EShooterPushPhase ResolvePhase(const FInstanceDataType& Data) const;

	/** Last Stand only: is there room around the target for the charge-and-orbit, or is this a
	 *  tight corner that should just get a straight approach instead? See OrbitOpenRadius. */
	bool HasRoomToOrbit(const FInstanceDataType& Data) const;

	/** Start a new leg in the direction this phase wants. */
	void StartLeg(FInstanceDataType& Data) const;

	/** Direction the current leg should run, already angled off the straight line. */
	FVector ComputeLegDirection(const FInstanceDataType& Data) const;

	void StartShooting(FInstanceDataType& Data) const;
	void StopShooting(FInstanceDataType& Data) const;

	/** Everything that has to be undone when the task or the phase ends: sprint latch, fire. */
	void ReleaseMovementState(FInstanceDataType& Data) const;
};

// ============================================================================
// Shield state - the Push/Peek gate.
//
// "Shield" on an enemy is the charge meter, not a prop: ionization FILLS it, and the shield is down
// once the charge sits at its own ceiling. The canonical answer already exists as
// AShooterWeapon::IsTargetShieldDown, which reads UEMFVelocityModifier::IsAtMaxCharge, and these
// conditions deliberately go through the same component so that "the weapon may hurt it" and "it
// should stop pushing" can never disagree.
//
// Two structs rather than one with an invert flag, because that is how the knockback pair next door
// is already shaped and a tree reads better with the intent spelled out.
//
// The magnitude is what matters, not the sign: charge runs both ways and either extreme is a broken
// shield. IsAtMaxCharge already compares FMath::Abs(GetCharge()), so nothing here re-derives it.
//
// An enemy carrying no charge component at all answers NOT down, i.e. it keeps pushing. This is on
// purpose and it is the one place these differ from the weapon's gate: the weapon asks "may I hurt
// this", and a chargeless target is freely hurtable; the tree asks "has my shield broken", and an
// enemy that never had the mechanic has not lost anything.
// ============================================================================

USTRUCT()
struct FSTCondition_ShooterShield_Data
{
	GENERATED_BODY()

	/** The NPC whose shield is being asked about. */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AShooterNPC> NPC;
};

/** True once this NPC's charge has reached its ceiling, i.e. the shield is gone. */
USTRUCT(DisplayName = "Shield Is Down", Category = "Polarity|AI|Shooter")
struct POLARITY_API FSTCondition_ShooterShieldDown : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_ShooterShield_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

/** Whether this NPC's class is allowed to close distance at all.
 *
 *  An ENTER CONDITION, and it has to be one. A task that refuses on entry does not hand selection to
 *  the next sibling: the state completes as failed, its own transitions are consulted, and with none
 *  matching, the tree walks on to whatever comes after - which for Armed States is the roaming
 *  search, so the enemy wanders off with its back turned. Only an enter condition makes selection
 *  skip a state and try the next one, which is the behaviour a class that never pushes needs.
 *
 *  Put this on Root/Armed States/Push. An NPC with no profile answers TRUE, so the shared tree keeps
 *  working unchanged for every enemy that has not been given a class. */
USTRUCT(DisplayName = "Class Can Push", Category = "Polarity|AI|Shooter")
struct POLARITY_API FSTCondition_ClassCanPush : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_ShooterShield_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

/** True once this NPC has recovered a real amount of shield. Deliberately NOT the exact negation of
 *  the pair above: "at the ceiling" is a knife edge, and the recovery curve steps off it on its very
 *  first frame, so an exact negation flips to true the instant the curve removes one unit of charge.
 *  The enemy would then leave cover a fraction of a second after reaching it, which reads in game as
 *  "it never hides at all".
 *
 *  Hysteresis instead: the shield breaks at the ceiling and counts as restored only once the charge
 *  has fallen back to RecoveredFraction of that ceiling. Between the two the answer is neither, so
 *  whichever state the tree is already in keeps running - which is exactly the cycle wanted. */
USTRUCT(DisplayName = "Shield Is Up", Category = "Polarity|AI|Shooter")
struct POLARITY_API FSTCondition_ShooterShieldUp : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_ShooterShield_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	/** Доля потолка заряда, до которой надо откатиться, чтобы щит считался восстановленным.
	 *  0.5 = «половина шкалы снята». Единица вернула бы прежнее поведение ножа: щит «цел» сразу
	 *  после первого кадра отката. Ноль означал бы «только полностью разряженный щит считается
	 *  целым», то есть NPC сидел бы в укрытии до самого конца кривой. */
	UPROPERTY(EditAnywhere, Category = "Shield", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RecoveredFraction = 0.5f;

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

// ============================================================================
// ShooterPeek - what an enemy does once its shield is gone.
//
// The mirror of ShooterPush. Push is how a SHIELDED enemy closes; this is how a broken one trades.
// It does not choose the corner itself. UCoverFinderComponent already does that: an EQS ring around
// the NPC, filtered by distance to the target and by reachability, scored by exposure weighted per
// player threat, handing back a hide/peek PAIR. This task is only the rhythm that uses the pair.
//
//   Seeking   no spot yet          ask, and keep asking while the cooldown allows
//   ToHide    walking to H         silent
//   AtHide    behind cover         silent, re-checks that H is still worth standing in
//   ToPeek    stepping out to P    silent
//   AtPeek    exposed near P       STRAFING and firing, flipping direction mid-burst, then back
//
// AtPeek never stands still, and that is the one thing about it that is not negotiable. The push
// next door exists because RunAndShoot was a stop-and-shoot: it braked, waited for its own velocity
// to fall, and only then fired, so movement and fire were mutually exclusive by construction and a
// row of enemies took turns shooting from a standstill. A peek that plants itself at P and empties a
// magazine is the same mistake wearing a corner. So the burst runs ACROSS the lateral legs: fire
// starts once on arrival, the legs flip underneath it, and the direction change is something the
// player sees happen in the middle of being shot at.
//
// Why a beat rather than "fire until the target dies": P is by construction a place the target CAN
// see, so an enemy that steps out and stays out is just a moving target that left its cover. The
// return to H is the whole point of the pair, and PeekLeash is what keeps the strafe from turning
// into a relocation.
//
// Why the re-check in AtHide: players move. A spot that hid from everybody stops hiding without
// anything happening to the NPC, and UCoverFinderComponent::IsCoverStillGood is the cheap way to
// notice (one trace per living player).
// ============================================================================

/** Where in the hide/peek rhythm this NPC currently is. */
UENUM()
enum class EShooterPeekPhase : uint8
{
	Seeking,
	ToHide,
	AtHide,
	ToPeek,
	AtPeek
};

USTRUCT()
struct FSTTask_ShooterPeek_Data
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AShooterNPC> NPC;

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	// ---- Rhythm (test values, design doc 5.10) ----

	/** How long it stays behind cover before stepping out again. */
	UPROPERTY(EditAnywhere, Category = "Rhythm", meta = (ClampMin = "0.0"))
	float HideDuration = 1.5f;

	/** Total time spent exposed, strafing and firing, before ducking back.
	 *
	 *  Must stay comfortably above StrafeHoldMax or the whole point is lost: the burst has to
	 *  outlive at least one leg so the direction change happens WHILE the NPC is still shooting.
	 *  At the defaults this is three to four legs. */
	UPROPERTY(EditAnywhere, Category = "Rhythm", meta = (ClampMin = "0.1"))
	float PeekDuration = 2.0f;

	// ---- Strafe while firing ----

	/** How long one lateral leg runs before the direction flips. Short on purpose: the flip is the
	 *  read, and it has to land inside the burst rather than between two bursts. */
	UPROPERTY(EditAnywhere, Category = "Strafe", meta = (ClampMin = "0.1"))
	float StrafeHoldMin = 0.45f;

	UPROPERTY(EditAnywhere, Category = "Strafe", meta = (ClampMin = "0.1"))
	float StrafeHoldMax = 0.8f;

	/** How far the strafe may wander from P. Without a leash the lateral legs would walk the NPC
	 *  off its own corner and into the open, which is the one thing the hide/peek pair exists to
	 *  prevent: P is chosen to be a step out of cover, not a new position. */
	UPROPERTY(EditAnywhere, Category = "Strafe", meta = (ClampMin = "0.0"))
	float PeekLeash = 250.0f;

	/** How close counts as arrived. The H/P pair is only PeekStepDistance apart, so this has to stay
	 *  well under that or the two ends collapse into one. */
	UPROPERTY(EditAnywhere, Category = "Rhythm", meta = (ClampMin = "10.0"))
	float ArriveRadius = 60.0f;

	/** How often, while hiding, the current spot is re-checked against the players who moved. */
	UPROPERTY(EditAnywhere, Category = "Rhythm", meta = (ClampMin = "0.1"))
	float CoverRecheckInterval = 1.0f;

	/** Seconds of NO PROGRESS toward the goal before the walk is written off and a different corner
	 *  is requested. Not a pacing knob, a safety net: something standing in the doorway of H would
	 *  otherwise hold the NPC in ToHide for the rest of the fight, and a peek that never peeks looks
	 *  exactly like a broken tree.
	 *
	 *  It measures being stuck, NOT how long the walk has run, and the difference is not academic.
	 *  As an absolute clock it cut every single walk short: measured 2026-08-18, an NPC closing on a
	 *  corner at full speed was stopped 94 units from a 60 unit arrival radius, dropped the claim,
	 *  and the next search sent it 1700 units the other way. Not one walk in the session finished. */
	UPROPERTY(EditAnywhere, Category = "Rhythm", meta = (ClampMin = "0.5"))
	float MoveTimeout = 5.0f;

	// ---- Re-evaluating the corner ----

	/** How often, while hiding, the NPC asks whether somewhere better has opened up.
	 *
	 *  Separate from CoverRecheckInterval, which only asks whether the CURRENT spot still hides.
	 *  This one is the ambitious question, and it is the difference between an enemy that holds a
	 *  corner until it stops working and one that keeps reading the room. The search itself is the
	 *  component's, so "better" means lower exposure weighted by per-player threat, and the spot can
	 *  never be one the players are standing on: the query already rejects anything closer to the
	 *  target than MinPeekDistance. */
	UPROPERTY(EditAnywhere, Category = "Reposition", meta = (ClampMin = "0.5"))
	float OpportunisticSearchInterval = 3.0f;

	// ---- Last stand ----
	//
	// Reported by returning Succeeded, which this task does for no other reason. The tree turns that
	// into the transition to the cornered push. Kept as a run status rather than an output flag so
	// the tree needs no extra condition to read it.

	/** A player this close to the NPC means the corner has been pushed and there is nothing left to
	 *  peek from. Measured to the NPC, not to its hide spot, because by the time they are on top of
	 *  it the spot is behind them. */
	UPROPERTY(EditAnywhere, Category = "Last Stand", meta = (ClampMin = "0.0"))
	float LastStandPlayerDistance = 700.0f;

	/** This many searches in a row came back with nothing: there is no line of retreat left. Counted
	 *  on completed searches only, so a request refused by the cooldown does not count as a
	 *  failure. */
	UPROPERTY(EditAnywhere, Category = "Last Stand", meta = (ClampMin = "1"))
	int32 LastStandFailedSearches = 3;

	// ---- Squad: relocation under covering fire ----

	/** A walk to a new corner longer than this is a RUN, and is announced to the squad so somebody
	 *  covers it. Below it the NPC just walks: a short shuffle behind the same wall does not need
	 *  two teammates standing in the open on its behalf. */
	UPROPERTY(EditAnywhere, Category = "Squad", meta = (ClampMin = "0.0"))
	float RelocationSprintDistance = 900.0f;

	/** Estimated seconds under fire above which the run asks for covering fire rather than simply
	 *  sprinting. Compared against the path cost the cover component measured, so it is in the same
	 *  currency: seconds visible, weighted by how dangerous the watcher is. */
	UPROPERTY(EditAnywhere, Category = "Squad", meta = (ClampMin = "0.0"))
	float CoveringFireThreshold = 1.5f;

	/** While suppressing, the NPC holds its peek instead of running the hide/peek cycle. It still
	 *  strafes - a stationary suppressor is a free kill - but the strafe is leashed to the peek
	 *  point and it never withdraws behind cover until the run it is covering is over. */
	UPROPERTY(EditAnywhere, Category = "Squad", meta = (ClampMin = "0.0"))
	float SuppressionStrafeLeash = 250.0f;

	// ---- Runtime ----

	EShooterPeekPhase Phase = EShooterPeekPhase::Seeking;

	/** Seconds spent in the current phase. */
	float PhaseElapsed = 0.0f;

	/** Seconds since the last IsCoverStillGood call. */
	float SinceRecheck = 0.0f;

	/** Seconds since the last "is there anywhere better" search. */
	float SinceOpportunisticSearch = 0.0f;

	/** Completed searches in a row that found nothing. Reset by any success. */
	int32 FailedSearches = 0;

	/** Previous frame's IsSearching, so a search COMPLETING can be told from one still running.
	 *  Without the edge there is no moment at which a failure can be counted. */
	bool bWasSearching = false;

	/** Which way the current strafe leg runs. Flips at the end of every leg. */
	float LegSign = 1.0f;

	/** Seconds spent in the current strafe leg, and how long this one was rolled to last. */
	float LegElapsed = 0.0f;
	float LegDuration = 0.0f;

	bool bIsShooting = false;

	/** Closest this NPC has been to the current move goal, and seconds since that last improved.
	 *  MoveTimeout is spent against the SECOND of these, not against PhaseElapsed: an NPC that is
	 *  still closing is not stuck, however long the walk takes. Measured 2026-08-18: a corner 5.0s
	 *  away was abandoned 94 units short of a 60 unit arrival radius, and the re-search then handed
	 *  out a corner 1700 units in the other direction, so the walk never finished even once. */
	float BestGoalDistance = 0.0f;
	float SinceGoalProgress = 0.0f;

	/** Set while this NPC is the one running, so the sprint, the silence and the body facing its
	 *  own direction of travel all end together and the coordinator gets told exactly once. */
	bool bIsRelocating = false;

	/** Seconds this corner has been held, across the whole hide/peek cycle rather than per phase.
	 *  MinCornerSeconds is measured against it, so a shoot-and-scoot class cannot bounce off a
	 *  corner it only just arrived at. */
	float CornerElapsed = 0.0f;

	/** Burst shot count seen last tick, so the moment a shot LANDS can be told from the state of
	 *  having fired at some point. A rocketeer leaves on the edge, not on the level. */
	int32 LastSeenBurstShots = 0;

	/** Set once this corner has produced a shot, which is what bRelocateAfterFiring waits for. Reset
	 *  with the corner, not with the peek: the point is one shot per POSITION. */
	bool bFiredFromCorner = false;

	/** Where the target was last actually visible. The grenadier shells this when it has no line;
	 *  zero until the first sighting, which is why bHasLastSeen exists separately. */
	FVector LastSeenTargetLocation = FVector::ZeroVector;
	bool bHasLastSeen = false;

	/** Who this NPC is suppressing for a teammate's run, or null. Not the same thing as Target: the
	 *  whole point is that it shoots the player who opened SOMEBODY ELSE'S corner, which is usually
	 *  not the one it was fighting. */
	UPROPERTY()
	TObjectPtr<APawn> SuppressionTarget = nullptr;

	/** Whether this NPC has stepped out at least once since it took THIS hide spot. Until it has,
	 *  the exposure recheck may not throw it back into the search: the recheck runs on a 1.0s clock
	 *  and the hide lasts 1.5s, so on an open arena it always got the first word and the peek never
	 *  happened at all. Reset when a new spot is adopted, not when returning from a peek. */
	bool bPeekedSinceCover = false;
};

USTRUCT(DisplayName = "Shooter Peek", Category = "Polarity|AI|Shooter")
struct FSTTask_ShooterPeek : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ShooterPeek_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif

private:

	/** Move to Location, or report false when no path could be issued. */
	bool TryMoveTo(FInstanceDataType& Data, const FVector& Location) const;

	/** Issue one lateral leg around the target, leashed to Anchor, flipping the side if the point
	 *  will not project onto the navmesh. Never touches the fire state: the burst runs across leg
	 *  boundaries, which is what makes the direction change happen mid-burst. */
	void StartStrafeLeg(FInstanceDataType& Data, const FVector& Anchor) const;

	void EnterPhase(FInstanceDataType& Data, EShooterPeekPhase NewPhase) const;

	/** Book-keeping for one tick of a move phase. Returns true once the NPC has gone MoveTimeout
	 *  seconds without getting meaningfully closer to DistanceToGoal, i.e. it is actually stuck
	 *  rather than merely slow. */
	bool IsMoveStalled(FInstanceDataType& Data, float DeltaTime, float DistanceToGoal) const;

	/** True when the path-following component has already given up on the current move (gone
	 *  Idle) while the NPC is not actually at the goal. Distinct from IsMoveStalled: this catches
	 *  it the tick it happens, MoveTimeout catches it only after the NPC has been motionless for
	 *  the FULL timeout. A short grace period guards the one this is not: the frame or two right
	 *  after issuing a fresh MoveTo, where the path follower reads Idle simply because it has not
	 *  started moving yet. */
	bool HasPathFollowingGivenUp(const FInstanceDataType& Data) const;

	/** Ask the squad whether this NPC should be covering somebody's run right now, and take or drop
	 *  the suppression duty accordingly. Routed through AShooterAIController::DistractTo, the same
	 *  path a decoy uses, so there is exactly one mechanism in the project for "look at this and
	 *  ignore your senses". Returns whether the NPC is suppressing after the call. */
	bool UpdateSuppressionDuty(FInstanceDataType& Data) const;

	/** Announce a long walk to the squad and switch the body into a run. Returns whether the squad
	 *  granted a slot; a refusal means somebody else is already running and this NPC should keep its
	 *  corner for now. */
	bool BeginRelocationRun(FInstanceDataType& Data, float Distance) const;

	/** Stop running: tell the squad, put the body back on the target, drop the sprint. Safe to call
	 *  when no run is in progress. */
	void EndRelocationRun(FInstanceDataType& Data) const;

	void StartShooting(FInstanceDataType& Data, AActor* ShootAt) const;
	void StopShooting(FInstanceDataType& Data) const;

	/** Fire and claim both released. Must run on EVERY exit: a leaked cover claim leaves a phantom
	 *  occupied corner that slowly squeezes the other NPCs into the open (design doc 5.5). */
	void ReleaseAll(FInstanceDataType& Data) const;
};
