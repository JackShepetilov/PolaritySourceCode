// FlyingDroneStateTreeTasks.h
// StateTree Tasks and Conditions specific to FlyingDrone (flight and evasion)

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "FlyingDroneStateTreeTasks.generated.h"

class AFlyingDrone;

//////////////////////////////////////////////////////////////////
// TASK: Drone Evasive Dash
// Performs an evasive dash in a random direction
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneEvasiveDashInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone that will dash (bind from Context: Actor) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;
};

USTRUCT(meta = (DisplayName = "Drone Evasive Dash", Category = "Flying Drone"))
struct POLARITY_API FStateTreeDroneEvasiveDashTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneEvasiveDashInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Drone Fly To Random Point
// Flies to a random patrol point within NavMesh bounds
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneFlyToRandomPointInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone that will move (bind from Context: Actor) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;

	/** Optional target to stay near (for combat positioning) */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	TObjectPtr<AActor> TargetToOrbit;

	/** Maximum distance from TargetToOrbit (if set), otherwise uses patrol radius */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "100", ClampMax = "5000"))
	float MaxDistanceFromTarget = 1500.0f;

	/** Minimum distance from TargetToOrbit (for combat spacing) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0", ClampMax = "2000"))
	float MinDistanceFromTarget = 500.0f;
};

USTRUCT(meta = (DisplayName = "Drone Fly To Random Point", Category = "Flying Drone"))
struct POLARITY_API FStateTreeDroneFlyToRandomPointTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneFlyToRandomPointInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Took Damage Recently
// Checks if drone took damage within grace period (for evasion trigger)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneTookDamageInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;

	/** Time window to consider "recent" damage (seconds) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float GracePeriod = 0.5f;
};

USTRUCT(DisplayName = "Drone Took Damage Recently", Category = "Flying Drone")
struct POLARITY_API FStateTreeDroneTookDamageCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneTookDamageInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Can Evasive Dash
// Checks if drone can perform evasive dash (cooldown check)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneCanEvasiveDashInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;
};

USTRUCT(DisplayName = "Drone Can Evasive Dash", Category = "Flying Drone")
struct POLARITY_API FStateTreeDroneCanEvasiveDashCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneCanEvasiveDashInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Is Flying
// Checks if drone is currently moving to a destination
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneIsFlyingInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;
};

USTRUCT(DisplayName = "Drone Is Flying", Category = "Flying Drone")
struct POLARITY_API FStateTreeDroneIsFlyingCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneIsFlyingInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Drone Is Dashing
// Checks if drone is currently performing a dash
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDroneIsDashingInstanceData
{
	GENERATED_BODY()

	/** FlyingDrone to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AFlyingDrone> Drone;
};

USTRUCT(DisplayName = "Drone Is Dashing", Category = "Flying Drone")
struct POLARITY_API FStateTreeDroneIsDashingCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDroneIsDashingInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};
