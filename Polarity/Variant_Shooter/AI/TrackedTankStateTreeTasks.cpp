// TrackedTankStateTreeTasks.cpp
// Implementation of StateTree Tasks and Conditions for ATrackedTankNPC.

#include "TrackedTankStateTreeTasks.h"
#include "TrackedTankNPC.h"
#include "StateTreeExecutionContext.h"
#include "Buildings/TurretBuilding.h"
#include "Buildings/BuildingMarkable.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "EngineUtils.h"

//////////////////////////////////////////////////////////////////
// CONDITION: Tank Target Is Building
//////////////////////////////////////////////////////////////////

bool FSTTreeTankTargetIsBuildingCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Target || Data.Target->IsPendingKillPending())
	{
		return false;
	}

	// The marker interface is what makes an actor a "building" for siege purposes today;
	// ATurretBuilding implements it. Broader building taxonomy can replace this later.
	return Data.Target->Implements<UBuildingMarkable>();
}

#if WITH_EDITOR
FText FSTTreeTankTargetIsBuildingCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Target is a building (main gun target)"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Tank Target Is Pawn
//////////////////////////////////////////////////////////////////

bool FSTTreeTankTargetIsPawnCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Target || Data.Target->IsPendingKillPending())
	{
		return false;
	}

	return Cast<APawn>(Data.Target) != nullptr;
}

#if WITH_EDITOR
FText FSTTreeTankTargetIsPawnCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Target is a pawn (machine gun target)"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Tank Is Immobilized
//////////////////////////////////////////////////////////////////

bool FSTTreeTankIsImmobilizedCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Tank)
	{
		return false;
	}

	return Data.Tank->IsImmobilized();
}

#if WITH_EDITOR
FText FSTTreeTankIsImmobilizedCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Tracks are broken - tank cannot move"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Tank Select Barrel
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FSTTask_TankSelectBarrel::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Tank || Data.Tank->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (Data.bSelectMainGun)
	{
		Data.Tank->SelectMainGun();
	}
	else
	{
		Data.Tank->SelectMachineGun();
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_TankSelectBarrel::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Tank || Data.Tank->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Keep enforcing while the state runs: a knockback interrupt or a sibling task may have
	// switched barrels behind our back
	if (Data.Tank->IsMainGunSelected() != Data.bSelectMainGun)
	{
		if (Data.bSelectMainGun)
		{
			Data.Tank->SelectMainGun();
		}
		else
		{
			Data.Tank->SelectMachineGun();
		}
	}

	return EStateTreeRunStatus::Running;
}

#if WITH_EDITOR
FText FSTTask_TankSelectBarrel::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return InstanceDataView.GetPtr<FInstanceDataType>() && InstanceDataView.GetPtr<FInstanceDataType>()->bSelectMainGun
		? FText::FromString(TEXT("Keep the MAIN GUN selected"))
		: FText::FromString(TEXT("Keep the MACHINE GUN selected"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Tank Find Siege Target
//////////////////////////////////////////////////////////////////

namespace
{
	/** Nearest standing building within the radius, or null. */
	AActor* FindNearestStandingBuilding(const ATrackedTankNPC* Tank, float SearchRadius)
	{
		if (!Tank || Tank->IsDead() || !Tank->GetWorld())
		{
			return nullptr;
		}

		const FVector Origin = Tank->GetActorLocation();

		AActor* Nearest = nullptr;
		float NearestDistSq = SearchRadius * SearchRadius;

		for (TActorIterator<ATurretBuilding> It(Tank->GetWorld()); It; ++It)
		{
			ATurretBuilding* Building = *It;
			if (!Building || Building->IsDead())
			{
				continue;
			}

			const float DistSq = FVector::DistSquared(Origin, Building->GetActorLocation());
			if (DistSq < NearestDistSq)
			{
				NearestDistSq = DistSq;
				Nearest = Building;
			}
		}

		return Nearest;
	}
}

EStateTreeRunStatus FSTTask_TankFindSiegeTarget::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	Data.FoundTarget = FindNearestStandingBuilding(Data.Tank, Data.SearchRadius);

	if (Data.FoundTarget)
	{
		UE_LOG(LogTemp, Log, TEXT("[TANK_DEBUG] %s found siege target %s"),
			*Data.Tank->GetName(), *Data.FoundTarget->GetName());
	}

	// This task runs as a GLOBAL task, and a global task that returns Failed or Succeeded
	// stops the whole tree before it ever selects a state: the tank then stands still with
	// StateTreeAI reported as NOT RUNNING, which is exactly what an empty level produced.
	// It is a sensor, like Sense Enemies: it publishes FoundTarget and keeps running.
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_TankFindSiegeTarget::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// Keep the output fresh, but only search again when there is nothing usable to publish:
	// buildings do not move, so a standing target stays valid until it falls.
	const ATurretBuilding* Current = Cast<ATurretBuilding>(Data.FoundTarget);
	if (!Current || Current->IsDead())
	{
		AActor* Previous = Data.FoundTarget;
		Data.FoundTarget = FindNearestStandingBuilding(Data.Tank, Data.SearchRadius);

		if (Data.FoundTarget && Data.FoundTarget != Previous)
		{
			UE_LOG(LogTemp, Log, TEXT("[TANK_DEBUG] %s found siege target %s"),
				*Data.Tank->GetName(), *Data.FoundTarget->GetName());
		}
	}

	return EStateTreeRunStatus::Running;
}

#if WITH_EDITOR
FText FSTTask_TankFindSiegeTarget::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Find nearest standing building -> FoundTarget"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Tank Siege Fire At Building
//////////////////////////////////////////////////////////////////

void FSTTask_TankSiegeFire::EndFiring(FSTTask_TankSiegeFireInstanceData& Data) const
{
	if (!Data.Tank)
	{
		return;
	}

	// StopShooting guards on its own state - safe to call unconditionally
	Data.Tank->StopShooting();
}

EStateTreeRunStatus FSTTask_TankSiegeFire::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Tank || Data.Tank->IsDead() || !Data.Target || Data.Target->IsPendingKillPending())
	{
		return EStateTreeRunStatus::Failed;
	}

	// The main gun is the only correct barrel against a structure
	Data.Tank->SelectMainGun();
	Data.Tank->StartShooting(Data.Target, true);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_TankSiegeFire::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Tank || Data.Tank->IsDead())
	{
		EndFiring(Data);
		return EStateTreeRunStatus::Failed;
	}

	const bool bTargetGone = !Data.Target || Data.Target->IsPendingKillPending();

	bool bTargetDown = bTargetGone;
	if (!bTargetDown)
	{
		// A collapsed building stops being a siege target
		if (ATurretBuilding* Building = Cast<ATurretBuilding>(Data.Target))
		{
			bTargetDown = Building->IsDead();
		}
	}

	if (bTargetDown)
	{
		EndFiring(Data);
		return EStateTreeRunStatus::Succeeded; // tree re-runs Find Siege Target
	}

	// Lost sight of the structure mid-burst - never fire through walls
	if (!bTargetGone && !Data.Tank->HasLineOfSightTo(Data.Target))
	{
		EndFiring(Data);
		return EStateTreeRunStatus::Failed; // let movement states regain LOS
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_TankSiegeFire::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	EndFiring(Data);
}

#if WITH_EDITOR
FText FSTTask_TankSiegeFire::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Fire main gun at building until it falls"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Tank Hold Position
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FSTTask_TankHoldPosition::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Tank || Data.Tank->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	Data.AnchorLocation = Data.Tank->GetActorLocation();
	Data.bReturningToAnchor = false;

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_TankHoldPosition::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Tank || Data.Tank->IsDead())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Broken tracks: hold from where we stand - re-anchor so a later repair does not send the
	// hull walking back to a position it can no longer defend
	if (Data.Tank->IsImmobilized())
	{
		Data.AnchorLocation = Data.Tank->GetActorLocation();
		Data.bReturningToAnchor = false;
		return EStateTreeRunStatus::Running;
	}

	AAIController* AI = Cast<AAIController>(Data.Tank->GetController());

	// Somebody else is driving (a squad advance order, a scripted move): the post travels with the
	// hull instead of yanking it back. Without this the leash wins every argument, and a tank told
	// to attack a POI 180 metres away oscillates around its spawn forever.
	if (!Data.bReturningToAnchor && AI && AI->GetPathFollowingComponent() &&
		AI->GetPathFollowingComponent()->GetStatus() == EPathFollowingStatus::Moving)
	{
		Data.AnchorLocation = Data.Tank->GetActorLocation();
		return EStateTreeRunStatus::Running;
	}

	const float DistToAnchorSq = FVector::DistSquared(Data.Tank->GetActorLocation(), Data.AnchorLocation);
	const float LeashSq = Data.LeashRadius * Data.LeashRadius;

	if (DistToAnchorSq > LeashSq && !Data.bReturningToAnchor)
	{
		if (AI && AI->MoveToLocation(Data.AnchorLocation, 100.0f))
		{
			Data.bReturningToAnchor = true;
		}
	}
	else if (Data.bReturningToAnchor && DistToAnchorSq <= 100.0f * 100.0f)
	{
		if (AI)
		{
			AI->StopMovement();
		}
		Data.bReturningToAnchor = false;
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_TankHoldPosition::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// Only cancel the move THIS task started. Stopping unconditionally cancels whatever else was
	// driving the hull, and this state is left often: the tree bounces through Root about once a
	// second whenever Siege finds no building, so a squad advance order was being torn down a
	// second after it was given, forever. Symptom was a tank with a valid four-point path, status
	// flipping Moving -> Idle, and zero velocity.
	if (Data.Tank && Data.bReturningToAnchor)
	{
		if (AAIController* AI = Cast<AAIController>(Data.Tank->GetController()))
		{
			AI->StopMovement();
		}
	}

	if (Data.Tank)
	{
		Data.bReturningToAnchor = false;
	}
}

#if WITH_EDITOR
FText FSTTask_TankHoldPosition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Anchor: stand ground, walk back if pushed beyond leash"));
}
#endif