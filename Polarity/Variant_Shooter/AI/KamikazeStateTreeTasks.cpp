// KamikazeStateTreeTasks.cpp

#include "KamikazeStateTreeTasks.h"
#include "KamikazeDroneNPC.h"
#include "StateTreeExecutionContext.h"
#include "AICombatCoordinator.h"

//////////////////////////////////////////////////////////////////
// TASK: Kamikaze Orbit
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FSTTask_KamikazeOrbit::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Drone should be in orbiting state
	if (Data.Drone->GetKamikazeState() != EKamikazeState::Orbiting)
	{
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_KamikazeOrbit::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Orbit update is driven by the drone's own Tick().
	// This task just monitors for attack trigger transitions.

	// If drone left orbiting state (retaliation/forced attack triggered in Tick),
	// signal Succeeded so StateTree can transition to Attack task
	if (Data.Drone->GetKamikazeState() != EKamikazeState::Orbiting)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

#if WITH_EDITOR
FText FSTTask_KamikazeOrbit::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Kamikaze orbit around player"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Kamikaze Attack
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FSTTask_KamikazeAttack::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// If drone isn't already in attack sequence (triggered by Tick retaliation/forced),
	// begin telegraph now
	if (!Data.Drone->IsInAttackSequence())
	{
		Data.Drone->BeginTelegraph(Data.bIsRetaliation);
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_KamikazeAttack::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	const EKamikazeState State = Data.Drone->GetKamikazeState();

	switch (State)
	{
	case EKamikazeState::Telegraphing:
	case EKamikazeState::Attacking:
	case EKamikazeState::PostAttack:
		// Still in attack sequence
		return EStateTreeRunStatus::Running;

	case EKamikazeState::Recovery:
		// Attack sequence complete — recovery. Keep running until recovery finishes.
		return EStateTreeRunStatus::Running;

	case EKamikazeState::Orbiting:
		// Recovery complete, back to orbit — success
		return EStateTreeRunStatus::Succeeded;

	case EKamikazeState::Dead:
		return EStateTreeRunStatus::Failed;

	default:
		return EStateTreeRunStatus::Failed;
	}
}

#if WITH_EDITOR
FText FSTTask_KamikazeAttack::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Kamikaze attack sequence (telegraph → dive → recovery)"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Kamikaze Should Attack
//////////////////////////////////////////////////////////////////

bool FSTCondition_KamikazeShouldAttack::TestCondition(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return false;
	}

	// Already in attack sequence (retaliation/forced triggered from Tick)
	if (Data.Drone->IsInAttackSequence())
	{
		return true;
	}

	// Retaliation: took damage while orbiting
	if (Data.Drone->IsRetaliating())
	{
		return true;
	}

	// Forced: orbit can't be maintained
	if (Data.Drone->IsOrbitForced())
	{
		return true;
	}

	// Check if coordinator has granted a token
	AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(Data.Drone);
	if (Coordinator && Coordinator->HasAttackToken(Data.Drone))
	{
		return true;
	}

	return false;
}

#if WITH_EDITOR
FText FSTCondition_KamikazeShouldAttack::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Kamikaze should attack (token/retaliation/forced)"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Kamikaze Is Orbiting
//////////////////////////////////////////////////////////////////

bool FSTCondition_KamikazeIsOrbiting::TestCondition(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return false;
	}

	return Data.Drone->GetKamikazeState() == EKamikazeState::Orbiting;
}

#if WITH_EDITOR
FText FSTCondition_KamikazeIsOrbiting::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Kamikaze is orbiting"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Kamikaze Took Damage
//////////////////////////////////////////////////////////////////

bool FSTCondition_KamikazeTookDamage::TestCondition(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Drone || Data.Drone->IsDead())
	{
		return false;
	}

	return Data.Drone->TookDamageRecently(Data.GracePeriod);
}

#if WITH_EDITOR
FText FSTCondition_KamikazeTookDamage::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Kamikaze took damage recently"));
}
#endif
