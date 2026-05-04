// HumanoidStateTreeTasks.cpp

#include "HumanoidStateTreeTasks.h"
#include "HumanoidNPC.h"
#include "StateTreeExecutionContext.h"

//////////////////////////////////////////////////////////////////
// CONDITION: Is In Ranged Mode
//////////////////////////////////////////////////////////////////

bool FStateTreeIsInRangedModeCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	return Data.Character && !Data.Character->bIsInMeleeMode;
}

#if WITH_EDITOR
FText FStateTreeIsInRangedModeCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Is In Ranged Mode (Humanoid)"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Is In Melee Mode
//////////////////////////////////////////////////////////////////

bool FStateTreeIsInMeleeModeCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	return Data.Character && Data.Character->bIsInMeleeMode;
}

#if WITH_EDITOR
FText FStateTreeIsInMeleeModeCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Is In Melee Mode (Humanoid)"));
}
#endif
