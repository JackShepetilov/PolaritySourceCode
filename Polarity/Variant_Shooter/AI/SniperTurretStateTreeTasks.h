// SniperTurretStateTreeTasks.h
// StateTree Tasks and Conditions for SniperTurretNPC

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "SniperTurretStateTreeTasks.generated.h"

class ASniperTurretNPC;

//////////////////////////////////////////////////////////////////
// TASK: Turret Aim at Target
// Drives the turret aim cycle: starts aiming on enter, polls LOS
// per tick, stops aiming on exit. The aim state machine (progress,
// fire, cooldown, damage recovery) lives inside ASniperTurretNPC.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeTurretAimInstanceData
{
	GENERATED_BODY()

	/** Turret NPC (bind from Context: Actor) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ASniperTurretNPC> Turret;

	/** Target to aim at (bind from Sense Enemies output) */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(meta = (DisplayName = "Turret Aim at Target", Category = "Sniper Turret"))
struct POLARITY_API FStateTreeTurretAimTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeTurretAimInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,
		const float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup,
		EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Is Turret Recovering
// Returns true if turret is in DamageRecovery state
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeIsTurretRecoveringInstanceData
{
	GENERATED_BODY()

	/** Turret to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ASniperTurretNPC> Turret;
};

USTRUCT(DisplayName = "Is Turret Recovering", Category = "Sniper Turret")
struct POLARITY_API FStateTreeIsTurretRecoveringCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeIsTurretRecoveringInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup,
		EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Is Turret Aiming
// Returns true if turret is actively aiming (progress advancing)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeIsTurretAimingInstanceData
{
	GENERATED_BODY()

	/** Turret to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ASniperTurretNPC> Turret;
};

USTRUCT(DisplayName = "Is Turret Aiming", Category = "Sniper Turret")
struct POLARITY_API FStateTreeIsTurretAimingCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeIsTurretAimingInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup,
		EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};
