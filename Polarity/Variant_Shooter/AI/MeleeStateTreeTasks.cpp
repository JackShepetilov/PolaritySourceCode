// MeleeStateTreeTasks.cpp
// Implementation of StateTree Tasks and Conditions for MeleeNPC

#include "MeleeStateTreeTasks.h"
#include "MeleeNPC.h"
#include "ShooterNPC.h"
#include "AIController.h"
#include "StateTreeExecutionContext.h"
#include "Navigation/PathFollowingComponent.h"
#include "AITypes.h"

//////////////////////////////////////////////////////////////////
// TASK: Melee Attack
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeMeleeAttackTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// Проверяем что всё валидно
	if (!Data.Character || !Data.Target)
	{
		UE_LOG(LogTemp, Warning, TEXT("MeleeAttackTask: Invalid Character or Target"));
		return EStateTreeRunStatus::Failed;
	}

	// Проверяем можем ли атаковать
	if (!Data.Character->CanAttack())
	{
		UE_LOG(LogTemp, Verbose, TEXT("MeleeAttackTask: Cannot attack (cooldown or dead)"));
		return EStateTreeRunStatus::Failed;
	}

	// Проверяем дистанцию
	if (!Data.Character->IsTargetInAttackRange(Data.Target))
	{
		UE_LOG(LogTemp, Verbose, TEXT("MeleeAttackTask: Target not in range"));
		return EStateTreeRunStatus::Failed;
	}

	// Начинаем атаку
	Data.Character->StartMeleeAttack(Data.Target);

	UE_LOG(LogTemp, Verbose, TEXT("MeleeAttackTask: Started attack on %s"), *Data.Target->GetName());

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeMeleeAttackTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Character || !Data.Target)
	{
		return EStateTreeRunStatus::Failed;
	}

	bool bIsAttacking = Data.Character->IsAttacking();
	bool bCanAttack = Data.Character->CanAttack();
	bool bInRange = Data.Character->IsTargetInAttackRange(Data.Target);
	bool bInKnockback = Data.Character->IsInKnockback();

	UE_LOG(LogTemp, Warning, TEXT("MeleeAttackTask::Tick - Attacking=%s, CanAttack=%s, InRange=%s, InKnockback=%s"),
		bIsAttacking ? TEXT("Y") : TEXT("N"),
		bCanAttack ? TEXT("Y") : TEXT("N"),
		bInRange ? TEXT("Y") : TEXT("N"),
		bInKnockback ? TEXT("Y") : TEXT("N"));

	if (bIsAttacking)
	{
		return EStateTreeRunStatus::Running;
	}

	if (bCanAttack && bInRange)
	{
		Data.Character->StartMeleeAttack(Data.Target);
		return EStateTreeRunStatus::Running;
	}

	if (!bInRange)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeMeleeAttackTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Ничего особенного не нужно делать
	// Атака сама завершится по таймеру/анимации
}

#if WITH_EDITOR
FText FStateTreeMeleeAttackTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Execute melee attack on target"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Is In Melee Range
//////////////////////////////////////////////////////////////////

bool FStateTreeIsInMeleeRangeCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Character || !Data.Target)
	{
		return false;
	}

	return Data.Character->IsTargetInAttackRange(Data.Target);
}

#if WITH_EDITOR
FText FStateTreeIsInMeleeRangeCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Target is within melee attack range"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Can Melee Attack
//////////////////////////////////////////////////////////////////

bool FStateTreeCanMeleeAttackCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Character)
	{
		return false;
	}

	return Data.Character->CanAttack();
}

#if WITH_EDITOR
FText FStateTreeCanMeleeAttackCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("NPC can perform melee attack (not in cooldown)"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Is NPC Dead
//////////////////////////////////////////////////////////////////

bool FStateTreeIsNPCDeadCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Character)
	{
		return true; // Если нет персонажа - считаем мёртвым
	}

	return Data.Character->IsDead();
}

#if WITH_EDITOR
FText FStateTreeIsNPCDeadCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("NPC is dead"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Is In Knockback
//////////////////////////////////////////////////////////////////

bool FStateTreeIsInKnockbackCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Character)
	{
		return false;
	}

	return Data.Character->IsInKnockback();
}

#if WITH_EDITOR
FText FStateTreeIsInKnockbackCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("NPC is in knockback state"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Has Valid Target
//////////////////////////////////////////////////////////////////

bool FStateTreeHasValidTargetCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	bool bIsValid = Data.Target != nullptr && IsValid(Data.Target);

	UE_LOG(LogTemp, Verbose, TEXT("HasValidTarget: Target=%s, IsValid=%s"),
		Data.Target ? *Data.Target->GetName() : TEXT("NULL"),
		bIsValid ? TEXT("TRUE") : TEXT("FALSE"));

	return bIsValid;
}

#if WITH_EDITOR
FText FStateTreeHasValidTargetCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Has a valid target actor"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Is NOT In Knockback
//////////////////////////////////////////////////////////////////

bool FStateTreeIsNotInKnockbackCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Character)
	{
		return true; // Если нет персонажа - считаем что NOT in knockback (безопасный default)
	}

	return !Data.Character->IsInKnockback();
}

#if WITH_EDITOR
FText FStateTreeIsNotInKnockbackCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("NPC is NOT in knockback state (recovered)"));
}
#endif

//////////////////////////////////////////////////////////////////
// TASK: Melee Dash
//////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeMeleeDashTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// Проверяем валидность Character и Target
	if (!Data.Character || !Data.Target)
	{
		UE_LOG(LogTemp, Warning, TEXT("MeleeDashTask: Invalid Character or Target"));
		return EStateTreeRunStatus::Failed;
	}

	// Проверяем можем ли выполнить рывок
	if (!Data.Character->CanDash())
	{
		UE_LOG(LogTemp, Verbose, TEXT("MeleeDashTask: Cannot dash (cooldown or other state)"));
		return EStateTreeRunStatus::Failed;
	}

	// Вычисляем направление к цели
	FVector ToTarget = (Data.Target->GetActorLocation() - Data.Character->GetActorLocation()).GetSafeNormal2D();

	if (ToTarget.IsNearlyZero())
	{
		UE_LOG(LogTemp, Warning, TEXT("MeleeDashTask: Target is at same location"));
		return EStateTreeRunStatus::Failed;
	}

	// Вычисляем финальное направление рывка в зависимости от настройки
	FVector FinalDirection;

	switch (Data.DashDirection)
	{
	case EDashDirection::Forward:
		// Рывок к цели
		FinalDirection = ToTarget;
		break;

	case EDashDirection::Left:
		// Перпендикуляр влево от направления на цель
		FinalDirection = FVector::CrossProduct(ToTarget, FVector::UpVector).GetSafeNormal();
		break;

	case EDashDirection::Right:
		// Перпендикуляр вправо от направления на цель
		FinalDirection = -FVector::CrossProduct(ToTarget, FVector::UpVector).GetSafeNormal();
		break;

	case EDashDirection::RandomSide:
	default:
		// Случайно влево или вправо
		{
			FVector Perpendicular = FVector::CrossProduct(ToTarget, FVector::UpVector).GetSafeNormal();
			FinalDirection = FMath::RandBool() ? Perpendicular : -Perpendicular;
		}
		break;
	}

	// Запускаем рывок
	bool bDashStarted = Data.Character->StartDash(FinalDirection, Data.DashDistance);

	if (!bDashStarted)
	{
		UE_LOG(LogTemp, Verbose, TEXT("MeleeDashTask: StartDash failed (path validation)"));
		return EStateTreeRunStatus::Failed;
	}

	UE_LOG(LogTemp, Verbose, TEXT("MeleeDashTask: Started dash, Direction=%s, Distance=%.1f"),
		*FinalDirection.ToString(), Data.DashDistance);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeMeleeDashTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Character)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Проверяем выполняется ли ещё рывок
	if (Data.Character->IsDashing())
	{
		return EStateTreeRunStatus::Running;
	}

	// Рывок завершён
	return EStateTreeRunStatus::Succeeded;
}

void FStateTreeMeleeDashTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Ничего особенного не нужно - рывок сам завершится или отменится при knockback
}

#if WITH_EDITOR
FText FStateTreeMeleeDashTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Execute dash movement towards or around target"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Can Dash
//////////////////////////////////////////////////////////////////

bool FStateTreeCanDashCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Character)
	{
		return false;
	}

	return Data.Character->CanDash();
}

#if WITH_EDITOR
FText FStateTreeCanDashCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("MeleeNPC can perform dash (not in cooldown)"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Is Dashing
//////////////////////////////////////////////////////////////////

bool FStateTreeIsDashingCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Character)
	{
		return false;
	}

	return Data.Character->IsDashing();
}

#if WITH_EDITOR
FText FStateTreeIsDashingCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("MeleeNPC is currently dashing"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Distance To Target In Range
//////////////////////////////////////////////////////////////////

bool FStateTreeDistanceToTargetCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Character || !Data.Target)
	{
		return false;
	}

	float Distance = FVector::Dist(Data.Character->GetActorLocation(), Data.Target->GetActorLocation());
	bool bInRange = Distance >= Data.MinDistance && Distance <= Data.MaxDistance;

	UE_LOG(LogTemp, Verbose, TEXT("DistanceToTarget: Distance=%.2f, Min=%.2f, Max=%.2f, InRange=%s"),
		Distance, Data.MinDistance, Data.MaxDistance, bInRange ? TEXT("YES") : TEXT("NO"));

	return bInRange;
}

#if WITH_EDITOR
FText FStateTreeDistanceToTargetCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Distance to target is within specified range"));
}
#endif
