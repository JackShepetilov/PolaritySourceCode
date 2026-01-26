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
