// BossAIController.cpp
// AI Controller for the hybrid boss character

#include "BossAIController.h"
#include "BossCharacter.h"
#include "Variant_Shooter/AI/FlyingAIMovementComponent.h"

ABossAIController::ABossAIController()
{
}

void ABossAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Cache boss reference
	ControlledBoss = Cast<ABossCharacter>(InPawn);

	// Cache flying movement component
	if (ControlledBoss)
	{
		FlyingMovement = ControlledBoss->GetFlyingMovement();
	}
}

void ABossAIController::OnUnPossess()
{
	ControlledBoss = nullptr;
	FlyingMovement = nullptr;

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

// ==================== Aerial Phase Commands ====================

void ABossAIController::StartHovering()
{
	if (ControlledBoss)
	{
		ControlledBoss->StartHovering();
	}
}

void ABossAIController::StopHovering()
{
	if (ControlledBoss)
	{
		ControlledBoss->StopHovering();
	}
}

void ABossAIController::AerialStrafe(const FVector& Direction)
{
	if (ControlledBoss)
	{
		ControlledBoss->AerialStrafe(Direction);
	}
}

bool ABossAIController::PerformAerialDash()
{
	if (!ControlledBoss)
	{
		return false;
	}

	return ControlledBoss->PerformAerialDash();
}

void ABossAIController::FireEMFProjectile(AActor* Target)
{
	if (ControlledBoss)
	{
		ControlledBoss->FireEMFProjectile(Target);
	}
}

void ABossAIController::MatchOppositePolarity(AActor* Target)
{
	if (ControlledBoss)
	{
		ControlledBoss->MatchOppositePolarity(Target);
	}
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

bool ABossAIController::ShouldTransitionToAerial() const
{
	if (!ControlledBoss)
	{
		return false;
	}

	return ControlledBoss->ShouldTransitionToAerial();
}

bool ABossAIController::ShouldTransitionToGround() const
{
	if (!ControlledBoss)
	{
		return false;
	}

	return ControlledBoss->ShouldTransitionToGround();
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
