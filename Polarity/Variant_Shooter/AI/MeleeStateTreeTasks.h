// MeleeStateTreeTasks.h
// StateTree Tasks and Conditions for MeleeNPC

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "MeleeStateTreeTasks.generated.h"

class AMeleeNPC;
class AShooterNPC;
class AAIController;

//////////////////////////////////////////////////////////////////
// TASK: Melee Attack
// Заставляет MeleeNPC атаковать цель
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeMeleeAttackInstanceData
{
	GENERATED_BODY()

	/** MeleeNPC который атакует (автоматически биндится из Context: Actor) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AMeleeNPC> Character;

	/** Цель для атаки (биндится к Output от SenseEnemies Task) */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(meta = (DisplayName = "Melee Attack", Category = "Melee"))
struct POLARITY_API FStateTreeMeleeAttackTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeMeleeAttackInstanceData;

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
// CONDITION: Is In Melee Range
// Проверяет находится ли цель в радиусе атаки
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeIsInMeleeRangeInstanceData
{
	GENERATED_BODY()

	/** MeleeNPC который проверяет дистанцию */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AMeleeNPC> Character;

	/** Цель для проверки */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(DisplayName = "Is In Melee Range", Category = "Melee")
struct POLARITY_API FStateTreeIsInMeleeRangeCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeIsInMeleeRangeInstanceData;

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
// CONDITION: Can Melee Attack
// Проверяет может ли NPC сейчас атаковать (не в кулдауне, не мёртв, не в knockback)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeCanMeleeAttackInstanceData
{
	GENERATED_BODY()

	/** MeleeNPC который проверяется */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AMeleeNPC> Character;
};

USTRUCT(DisplayName = "Can Melee Attack", Category = "Melee")
struct POLARITY_API FStateTreeCanMeleeAttackCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeCanMeleeAttackInstanceData;

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
// CONDITION: Is NPC Dead
// Проверяет мёртв ли NPC
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeIsNPCDeadInstanceData
{
	GENERATED_BODY()

	/** NPC для проверки (ShooterNPC или его наследник) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AShooterNPC> Character;
};

USTRUCT(DisplayName = "Is NPC Dead", Category = "Melee")
struct POLARITY_API FStateTreeIsNPCDeadCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeIsNPCDeadInstanceData;

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
// CONDITION: Is In Knockback
// Проверяет находится ли NPC в состоянии knockback
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeIsInKnockbackInstanceData
{
	GENERATED_BODY()

	/** NPC для проверки (ShooterNPC или его наследник) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AShooterNPC> Character;
};

USTRUCT(DisplayName = "Is In Knockback", Category = "Melee")
struct POLARITY_API FStateTreeIsInKnockbackCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeIsInKnockbackInstanceData;

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
// CONDITION: Has Valid Target
// Проверяет есть ли валидная цель (биндится к Output от SenseEnemies)
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeHasValidTargetInstanceData
{
	GENERATED_BODY()

	/** Цель для проверки (биндится к Output от SenseEnemies Task) */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;
};

USTRUCT(DisplayName = "Has Valid Target", Category = "Melee")
struct POLARITY_API FStateTreeHasValidTargetCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeHasValidTargetInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};
