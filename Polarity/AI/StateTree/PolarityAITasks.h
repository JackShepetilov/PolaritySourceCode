// PolarityAITasks.h
// StateTree tasks for Polarity AI system

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
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

	// ---- Runtime ----

	EShooterPushPhase Phase = EShooterPushPhase::Approach;
	EShooterPushPhase PreviousPhase = EShooterPushPhase::Approach;

	/** Which side the current leg is angled to. Flips at the end of every leg. */
	float LegSign = 1.0f;

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

	/** Start a new leg in the direction this phase wants. */
	void StartLeg(FInstanceDataType& Data) const;

	/** Direction the current leg should run, already angled off the straight line. */
	FVector ComputeLegDirection(const FInstanceDataType& Data) const;

	void StartShooting(FInstanceDataType& Data) const;
	void StopShooting(FInstanceDataType& Data) const;

	/** Everything that has to be undone when the task or the phase ends: sprint latch, fire. */
	void ReleaseMovementState(FInstanceDataType& Data) const;
};
