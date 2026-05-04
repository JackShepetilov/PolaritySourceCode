// HumanoidStateTreeTasks.h
// StateTree Conditions for HumanoidNPC mode-gating.
// NOTE: StateTree does not support condition inversion — both polarities are explicit structs.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "HumanoidStateTreeTasks.generated.h"

class AHumanoidNPC;

//////////////////////////////////////////////////////////////////
// CONDITION: Is In Ranged Mode
// True when humanoid still has weapons and can be yanked / shoot.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeIsInRangedModeInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AHumanoidNPC> Character;
};

USTRUCT(DisplayName = "Is In Ranged Mode (Humanoid)", Category = "Humanoid")
struct POLARITY_API FStateTreeIsInRangedModeCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeIsInRangedModeInstanceData;

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
// CONDITION: Is In Melee Mode
// True after all weapons yanked — humanoid fights melee-only.
//////////////////////////////////////////////////////////////////

USTRUCT()
struct FStateTreeIsInMeleeModeInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AHumanoidNPC> Character;
};

USTRUCT(DisplayName = "Is In Melee Mode (Humanoid)", Category = "Humanoid")
struct POLARITY_API FStateTreeIsInMeleeModeCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeIsInMeleeModeInstanceData;

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
