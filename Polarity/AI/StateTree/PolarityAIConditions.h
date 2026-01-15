// PolarityAIConditions.h
// StateTree conditions for Polarity AI system

#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "PolarityAIConditions.generated.h"

class APawn;
class AActor;
class AAICombatCoordinator;
class UMeleeRetreatComponent;

// ============================================================================
// IsPlayerMovingFast - Check if target is moving above speed threshold
// ============================================================================

USTRUCT()
struct FSTCondition_IsPlayerMovingFast_Data
{
	GENERATED_BODY()

	/** Target actor to check speed */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	/** Speed threshold (cm/s). Above this = "moving fast" */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0"))
	float SpeedThreshold = 400.0f;

	/** If true, condition passes when target IS moving fast */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bWantMovingFast = true;
};

USTRUCT(DisplayName = "Is Player Moving Fast", Category = "Polarity|AI")
struct FSTCondition_IsPlayerMovingFast : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_IsPlayerMovingFast_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, 
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

// ============================================================================
// IsPlayerWallRunning - Check if target is wall running
// ============================================================================

USTRUCT()
struct FSTCondition_IsPlayerWallRunning_Data
{
	GENERATED_BODY()

	/** Target actor to check */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	/** If true, condition passes when target IS wall running */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bWantWallRunning = true;
};

USTRUCT(DisplayName = "Is Player Wall Running", Category = "Polarity|AI")
struct FSTCondition_IsPlayerWallRunning : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_IsPlayerWallRunning_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

// ============================================================================
// HasAttackPermission - Check if NPC has permission from coordinator
// ============================================================================

USTRUCT()
struct FSTCondition_HasAttackPermission_Data
{
	GENERATED_BODY()

	/** The NPC pawn to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<APawn> NPC;
};

USTRUCT(DisplayName = "Has Attack Permission", Category = "Polarity|AI")
struct FSTCondition_HasAttackPermission : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_HasAttackPermission_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

// ============================================================================
// ShouldRetreat - Check if NPC should retreat (after melee hit)
// ============================================================================

USTRUCT()
struct FSTCondition_ShouldRetreat_Data
{
	GENERATED_BODY()

	/** The NPC pawn to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<APawn> NPC;
};

USTRUCT(DisplayName = "Should Retreat", Category = "Polarity|AI")
struct FSTCondition_ShouldRetreat : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_ShouldRetreat_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

// ============================================================================
// IsInCombatRange - Check if target is within weapon range
// ============================================================================

USTRUCT()
struct FSTCondition_IsInCombatRange_Data
{
	GENERATED_BODY()

	/** The NPC pawn */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<APawn> NPC;

	/** Target to check distance to */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Target;

	/** Minimum range (cm) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0"))
	float MinRange = 0.0f;

	/** Maximum range (cm) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0"))
	float MaxRange = 2000.0f;
};

USTRUCT(DisplayName = "Is In Combat Range", Category = "Polarity|AI")
struct FSTCondition_IsInCombatRange : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_IsInCombatRange_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};

// ============================================================================
// CanRetreat - Check if retreat is off cooldown
// ============================================================================

USTRUCT()
struct FSTCondition_CanRetreat_Data
{
	GENERATED_BODY()

	/** The NPC pawn to check */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<APawn> NPC;
};

USTRUCT(DisplayName = "Can Retreat", Category = "Polarity|AI")
struct FSTCondition_CanRetreat : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_CanRetreat_Data;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const override;
#endif
};
