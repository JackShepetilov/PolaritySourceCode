// KamikazeStateTreeTasks.h
// StateTree Tasks and Conditions for KamikazeDroneNPC

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "KamikazeDroneNPC.h"
#include "KamikazeStateTreeTasks.generated.h"

//////////////////////////////////////////////////////////////////
// TASK: Kamikaze Orbit
// Manages orbital behavior. Returns Running while orbiting,
// Succeeded when an attack trigger fires (for state transition).
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FSTTaskKamikazeOrbitInstanceData
{
	GENERATED_BODY()

	/** KamikazeDroneNPC to control (bind from Context: Actor) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AKamikazeDroneNPC> Drone;
};

USTRUCT(meta = (DisplayName = "Kamikaze Orbit", Category = "Kamikaze Drone"))
struct POLARITY_API FSTTask_KamikazeOrbit : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTaskKamikazeOrbitInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// TASK: Kamikaze Attack
// Manages the attack sequence: Telegraph → Attack → PostAttack → Recovery/Crash.
// Returns Succeeded on recovery, Failed on crash/death.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FSTTaskKamikazeAttackInstanceData
{
	GENERATED_BODY()

	/** KamikazeDroneNPC to control */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AKamikazeDroneNPC> Drone;

	/** If true, this attack is a retaliation (bypass token) */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bIsRetaliation = false;
};

USTRUCT(meta = (DisplayName = "Kamikaze Attack", Category = "Kamikaze Drone"))
struct POLARITY_API FSTTask_KamikazeAttack : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTaskKamikazeAttackInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

//////////////////////////////////////////////////////////////////
// CONDITION: Kamikaze Should Attack
// True if drone has token, or retaliation, or forced, or proximity timeout
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FSTConditionKamikazeShouldAttackInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AKamikazeDroneNPC> Drone;
};

USTRUCT(DisplayName = "Kamikaze Should Attack", Category = "Kamikaze Drone")
struct POLARITY_API FSTCondition_KamikazeShouldAttack : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTConditionKamikazeShouldAttackInstanceData;

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
// CONDITION: Kamikaze Is Orbiting
// True if drone state is Orbiting
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FSTConditionKamikazeIsOrbitingInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AKamikazeDroneNPC> Drone;
};

USTRUCT(DisplayName = "Kamikaze Is Orbiting", Category = "Kamikaze Drone")
struct POLARITY_API FSTCondition_KamikazeIsOrbiting : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTConditionKamikazeIsOrbitingInstanceData;

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
// CONDITION: Kamikaze Took Damage
// True if drone took damage within a grace period (for retaliation)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FSTConditionKamikazeTookDamageInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AKamikazeDroneNPC> Drone;

	/** Time window to consider "recent" damage */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float GracePeriod = 0.5f;
};

USTRUCT(DisplayName = "Kamikaze Took Damage", Category = "Kamikaze Drone")
struct POLARITY_API FSTCondition_KamikazeTookDamage : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTConditionKamikazeTookDamageInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};
