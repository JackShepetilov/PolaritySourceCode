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
