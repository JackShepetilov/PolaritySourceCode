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
class AShooterAIController;

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

//////////////////////////////////////////////////////////////////
// CONDITION: Is NOT In Knockback
// Проверяет что NPC НЕ находится в состоянии knockback
// Используется для transition из Stunned state обратно в Chase/Root
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeIsNotInKnockbackInstanceData
{
	GENERATED_BODY()

	/** NPC для проверки (ShooterNPC или его наследник) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AShooterNPC> Character;
};

USTRUCT(DisplayName = "Is NOT In Knockback", Category = "Melee")
struct POLARITY_API FStateTreeIsNotInKnockbackCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeIsNotInKnockbackInstanceData;

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
// ENUM: Направление рывка для MeleeDashTask
//////////////////////////////////////////////////////////////////

UENUM(BlueprintType)
enum class EDashDirection : uint8
{
	/** Рывок к цели (вперёд) */
	Forward UMETA(DisplayName = "Forward (To Target)"),
	/** Рывок влево от направления на цель */
	Left UMETA(DisplayName = "Left"),
	/** Рывок вправо от направления на цель */
	Right UMETA(DisplayName = "Right"),
	/** Случайный боковой рывок (влево или вправо) */
	RandomSide UMETA(DisplayName = "Random Side (Left or Right)")
};

//////////////////////////////////////////////////////////////////
// TASK: Melee Dash
// Выполняет рывок MeleeNPC в указанном направлении
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeMeleeDashInstanceData
{
	GENERATED_BODY()

	/** MeleeNPC который выполняет рывок (автоматически биндится из Context: Actor) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AMeleeNPC> Character;

	/** Цель для расчёта направления рывка (биндится к Output от SenseEnemies Task) */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	/** Дистанция рывка в см */
	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (ClampMin = "50", ClampMax = "500"))
	float DashDistance = 200.0f;

	/** Направление рывка относительно цели */
	UPROPERTY(EditAnywhere, Category = "Parameters")
	EDashDirection DashDirection = EDashDirection::RandomSide;
};

USTRUCT(meta = (DisplayName = "Melee Dash", Category = "Melee"))
struct POLARITY_API FStateTreeMeleeDashTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeMeleeDashInstanceData;

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
// CONDITION: Can Dash
// Проверяет может ли MeleeNPC выполнить рывок
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeCanDashInstanceData
{
	GENERATED_BODY()

	/** MeleeNPC для проверки */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AMeleeNPC> Character;
};

USTRUCT(DisplayName = "Can Dash", Category = "Melee")
struct POLARITY_API FStateTreeCanDashCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeCanDashInstanceData;

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
// CONDITION: Is Dashing
// Проверяет выполняет ли MeleeNPC рывок в данный момент
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeIsDashingInstanceData
{
	GENERATED_BODY()

	/** MeleeNPC для проверки */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AMeleeNPC> Character;
};

USTRUCT(DisplayName = "Is Dashing", Category = "Melee")
struct POLARITY_API FStateTreeIsDashingCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeIsDashingInstanceData;

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
// CONDITION: Distance To Target In Range
// Проверяет находится ли цель в заданном диапазоне расстояний
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeDistanceToTargetInstanceData
{
	GENERATED_BODY()

	/** Персонаж (NPC) от которого измеряется расстояние */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Character;

	/** Цель до которой измеряется расстояние */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	/** Минимальное расстояние (включительно) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0"))
	float MinDistance = 0.0f;

	/** Максимальное расстояние (включительно) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0"))
	float MaxDistance = 500.0f;
};

USTRUCT(DisplayName = "Distance To Target In Range", Category = "Melee")
struct POLARITY_API FStateTreeDistanceToTargetCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeDistanceToTargetInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

