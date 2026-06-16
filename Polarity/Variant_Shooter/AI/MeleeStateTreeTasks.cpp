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

	// Рывок невозможен (кулдаун / в воздухе / в прыжке) — НЕ двигаемся сами. Раньше здесь был
	// nav-fallback (свой MoveToActor с радиусом приёмки AttackRange*0.85), который утыкался в
	// НАЗЕМНУЮ проекцию игрока под уступом и топтался. Теперь возвращаем Failed, чтобы Chase выбрал
	// соседнюю ветку Pursue (встроенный Move To): она подбегает и лезет по навлинкам, как у стрелков.
	// Вертикаль отсекается ещё на ВХОДЕ в Dash условием TargetWithinDashVerticalRange, так что
	// вертикальные цели сюда уже не попадают — только планарные рывки.
	if (!Data.Character->CanDash())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] %s Dash: CanDash=false -> defer to Pursue"), *GetNameSafe(Data.Character));
		return EStateTreeRunStatus::Failed;
	}

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

	if (!Data.Character->StartDash(FinalDirection, Data.DashDistance))
	{
		// Рывок не стартовал (путь перекрыт / уже на нужной дистанции). Отдаём управление в Pursue.
		UE_LOG(LogTemp, Warning, TEXT("[MELEE_DEBUG] %s Dash: StartDash failed -> defer to Pursue"), *GetNameSafe(Data.Character));
		return EStateTreeRunStatus::Failed;
	}

	// [MELEE_DEBUG] Планарный рывок стартовал (цель в вертикальном диапазоне — гейт пропустил).
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

	// Ждём завершения рывка. Подбег к цели, когда рывок невозможен, делает ветка Pursue графа
	// (встроенный Move To), а не этот таск.
	if (Data.Character->IsDashing())
	{
		return EStateTreeRunStatus::Running;
	}
	return EStateTreeRunStatus::Succeeded;
}

void FStateTreeMeleeDashTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Рывок сам завершится или отменится при knockback — спец-очистки не требуется.
}

#if WITH_EDITOR
FText FStateTreeMeleeDashTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Execute dash movement towards or around target"));
}
#endif

//////////////////////////////////////////////////////////////////
// CONDITION: Target Within Dash Vertical Range
// True когда цель по высоте в пределах MaxDashVerticalDelta NPC. Вешается AND-ом в IF ветки Dash:
// если цель выше/ниже порога (false) — Dash НЕ входит, и Chase падает в Pursue (встроенный Move To),
// который лезет по навлинкам, как стрелки. Инверсии в StateTree нет, поэтому формулировка
// "в пределах" (true = dash можно), а не "слишком вертикально".
//////////////////////////////////////////////////////////////////

bool FStateTreeTargetWithinDashVerticalRangeCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.Character || !Data.Target)
	{
		// Нет цели/персонажа — рывок смысла не имеет. false, чтобы Dash не входил (Chase -> Pursue/Idle).
		return false;
	}

	const float VDelta = FMath::Abs(Data.Target->GetActorLocation().Z - Data.Character->GetActorLocation().Z);
	const float MaxDelta = Data.Character->GetMaxDashVerticalDelta();
	// БЕЗ лога: условие зовётся 3-5 раз за тик на каждого NPC (StateTree гоняет его при селекте и на
	// переходах) — это давало 7000+ строк спама за сессию и снова грозило контексту. Эффект гейта
	// виден по JUMP LAUNCH мили в логе пути (полез ли по навлинку).
	return VDelta <= MaxDelta;
}

#if WITH_EDITOR
FText FStateTreeTargetWithinDashVerticalRangeCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("Target is within dash vertical range (else: too high/low -> use Pursue/navigation)"));
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
