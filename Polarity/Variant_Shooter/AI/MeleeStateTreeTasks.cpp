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

	UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] MeleeAttackTask::EnterState — Character=%s, Target=%s"),
		Data.Character ? *Data.Character->GetName() : TEXT("NULL"),
		Data.Target ? *Data.Target->GetName() : TEXT("NULL"));

	// Проверяем что всё валидно
	if (!Data.Character || !Data.Target)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] MeleeAttackTask FAIL: Invalid Character or Target"));
		return EStateTreeRunStatus::Failed;
	}

	// Проверяем можем ли атаковать
	if (!Data.Character->CanAttack())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] MeleeAttackTask FAIL: CanAttack=false (see CanAttack log above for reason)"));
		return EStateTreeRunStatus::Failed;
	}

	// Проверяем дистанцию
	if (!Data.Character->IsTargetInAttackRange(Data.Target))
	{
		UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] MeleeAttackTask FAIL: Target not in range"));
		return EStateTreeRunStatus::Failed;
	}

	// Начинаем атаку
	Data.Character->StartMeleeAttack(Data.Target);

	UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] MeleeAttackTask STARTED on %s"), *Data.Target->GetName());

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
		UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] IsInMeleeRange = FALSE (Character or Target null)"));
		return false;
	}

	const float Distance = FVector::Dist(Data.Character->GetActorLocation(), Data.Target->GetActorLocation());
	const float Range = Data.Character->GetAttackRange();
	const bool bInRange = Distance <= Range;
	UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] IsInMeleeRange: dist=%.0f, range=%.0f, result=%d"),
		Distance, Range, bInRange ? 1 : 0);

	return bInRange;
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
		UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] CanMeleeAttack = FALSE (Character null)"));
		return false;
	}

	const bool bCan = Data.Character->CanAttack();
	UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] CanMeleeAttack: %d  (NPC=%s)"), bCan ? 1 : 0, *Data.Character->GetName());
	return bCan;
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

	const bool bIsValid = Data.Target != nullptr && IsValid(Data.Target);

	UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] HasValidTarget [owner=%s]: target=%s, valid=%d"),
		*GetNameSafe(Context.GetOwner()),
		Data.Target ? *Data.Target->GetName() : TEXT("NULL"),
		bIsValid ? 1 : 0);

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

	// Направление к цели (для рывка и разворота)
	FVector ToTarget = (Data.Target->GetActorLocation() - Data.Character->GetActorLocation()).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero())
	{
		ToTarget = Data.Character->GetActorForwardVector();
	}

	// Пытаемся выполнить рывок. Dash возможен только если CanDash() И путь планарный
	// (StartDash сам отсекает вертикаль/лестницу). Если рывок невозможен — НЕ падаем
	// (иначе melee застревал бы на лестнице, ведь атака идёт ПОСЛЕ этого таска), а
	// подбегаем к цели обычной навигацией (рампы + прыжки по навлинкам, как стрелки)
	// до радиуса атаки.
	bool bDashStarted = false;
	if (Data.Character->CanDash())
	{
		FVector FinalDirection;
		switch (Data.DashDirection)
		{
		case EDashDirection::Forward:
			FinalDirection = ToTarget;
			break;
		case EDashDirection::Left:
			FinalDirection = FVector::CrossProduct(ToTarget, FVector::UpVector).GetSafeNormal();
			break;
		case EDashDirection::Right:
			FinalDirection = -FVector::CrossProduct(ToTarget, FVector::UpVector).GetSafeNormal();
			break;
		case EDashDirection::RandomSide:
		default:
			{
				FVector Perpendicular = FVector::CrossProduct(ToTarget, FVector::UpVector).GetSafeNormal();
				FinalDirection = FMath::RandBool() ? Perpendicular : -Perpendicular;
			}
			break;
		}
		bDashStarted = Data.Character->StartDash(FinalDirection, Data.DashDistance);
	}

	if (!bDashStarted)
	{
		// Навигационный подбег к цели до радиуса атаки (Tick завершит таск при входе в радиус)
		Data.bUsingNavFallback = true;
		EPathFollowingRequestResult::Type MoveResult = EPathFollowingRequestResult::Failed;
		if (AAIController* AI = Cast<AAIController>(Data.Character->GetController()))
		{
			MoveResult = AI->MoveToActor(Data.Target, Data.Character->GetAttackRange() * 0.85f, true, true);
		}
		// [MELEE_DEBUG] Why this melee went nav-approach instead of dash, and whether the move was
		// even accepted. vDelta>120 => target is up high (should route via navlink); moveResult=0
		// (Failed) or a partial path => navmesh can't reach the target → NPC stalls at the wall foot.
		const float VDelta = Data.Target->GetActorLocation().Z - Data.Character->GetActorLocation().Z;
		const float Dist2D = FVector::Dist2D(Data.Character->GetActorLocation(), Data.Target->GetActorLocation());
		UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] %s DASH->NAV target=%s vDelta=%.0f dist2D=%.0f canDash=%d moveResult=%d"),
			*GetNameSafe(Data.Character), *GetNameSafe(Data.Target), VDelta, Dist2D,
			Data.Character->CanDash() ? 1 : 0, (int32)MoveResult);
		return EStateTreeRunStatus::Running;
	}

	Data.bUsingNavFallback = false;
	// [MELEE_DEBUG] A PLANAR dash was started. If this fires while the target is far above (vDelta
	// big), the dash is lunging at the wall instead of deferring to navigation.
	UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] %s DASH STARTED dist=%.0f vDelta=%.0f"),
		*GetNameSafe(Data.Character), Data.DashDistance,
		Data.Target->GetActorLocation().Z - Data.Character->GetActorLocation().Z);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeMeleeDashTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Character)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Навигационный подбег (dash был невозможен): вошли в радиус атаки — готово, граф
	// перейдёт в Attack. Иначе продолжаем движение, перезапуская MoveTo если оно встало.
	if (Data.bUsingNavFallback)
	{
		if (!Data.Target)
		{
			return EStateTreeRunStatus::Failed;
		}
		if (Data.Character->IsTargetInAttackRange(Data.Target))
		{
			return EStateTreeRunStatus::Succeeded;
		}
		if (AAIController* AI = Cast<AAIController>(Data.Character->GetController()))
		{
			if (AI->GetMoveStatus() == EPathFollowingStatus::Idle)
			{
				// [MELEE_DEBUG] The nav-approach went Idle without reaching attack range — i.e. the
				// path ENDED short (partial path to the wall foot / unreachable target up a navlink).
				// If this spams while dist2D stays ~constant and vDelta is big, THIS is the stomp.
				const float Dist2D = FVector::Dist2D(Data.Character->GetActorLocation(), Data.Target->GetActorLocation());
				const float VDelta = Data.Target->GetActorLocation().Z - Data.Character->GetActorLocation().Z;
				UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] %s NAV REISSUE (Idle, stalled) dist2D=%.0f vDelta=%.0f"),
					*GetNameSafe(Data.Character), Dist2D, VDelta);
				AI->MoveToActor(Data.Target, Data.Character->GetAttackRange() * 0.85f, true, true);
			}
		}
		return EStateTreeRunStatus::Running;
	}

	// Рывок: ждём завершения
	if (Data.Character->IsDashing())
	{
		return EStateTreeRunStatus::Running;
	}
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
