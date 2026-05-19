// BossAIController.cpp
// AI Controller for the ground-melee boss character

#include "BossAIController.h"
#include "BossCharacter.h"

ABossAIController::ABossAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void ABossAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	ControlledBoss = Cast<ABossCharacter>(InPawn);
}

void ABossAIController::OnUnPossess()
{
	ControlledBoss = nullptr;

	Super::OnUnPossess();
}

// ==================== Ground Phase Commands ====================

bool ABossAIController::StartApproachDash(AActor* Target)
{
	if (!ControlledBoss)
	{
		return false;
	}

	return ControlledBoss->StartApproachDash(Target);
}

bool ABossAIController::StartCircleDash(AActor* Target)
{
	if (!ControlledBoss)
	{
		return false;
	}

	return ControlledBoss->StartCircleDash(Target);
}

void ABossAIController::StartMeleeAttack(AActor* Target)
{
	if (ControlledBoss)
	{
		ControlledBoss->StartMeleeAttack(Target);
	}
}

bool ABossAIController::IsTargetInMeleeRange(AActor* Target) const
{
	if (!ControlledBoss)
	{
		return false;
	}

	return ControlledBoss->IsTargetInMeleeRange(Target);
}

// ==================== Phase Management ====================

void ABossAIController::SetPhase(EBossPhase NewPhase)
{
	if (ControlledBoss)
	{
		ControlledBoss->SetPhase(NewPhase);
	}
}

EBossPhase ABossAIController::GetCurrentPhase() const
{
	if (!ControlledBoss)
	{
		return EBossPhase::Ground;
	}

	return ControlledBoss->GetCurrentPhase();
}

// ==================== Finisher Phase ====================

void ABossAIController::EnterFinisherPhase()
{
	if (ControlledBoss)
	{
		ControlledBoss->EnterFinisherPhase();
	}
}

bool ABossAIController::IsInFinisherPhase() const
{
	if (!ControlledBoss)
	{
		return false;
	}

	return ControlledBoss->IsInFinisherPhase();
}

// ==================== State Queries ====================

bool ABossAIController::IsDashing() const
{
	if (!ControlledBoss)
	{
		return false;
	}

	return ControlledBoss->IsDashing();
}

bool ABossAIController::CanDash() const
{
	if (!ControlledBoss)
	{
		return false;
	}

	return ControlledBoss->CanDash();
}

bool ABossAIController::CanMeleeAttack() const
{
	if (!ControlledBoss)
	{
		return false;
	}

	return ControlledBoss->CanMeleeAttack();
}
