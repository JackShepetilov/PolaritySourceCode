// BossAIController.cpp
// AI Controller for the boss character.

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

	UE_LOG(LogTemp, Log, TEXT("[BOSS_AI] OnPossess: InPawn=%s -> ControlledBoss=%s"),
		*GetNameSafe(InPawn), *GetNameSafe(ControlledBoss));
}

void ABossAIController::OnUnPossess()
{
	ControlledBoss = nullptr;
	Super::OnUnPossess();
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
	return ControlledBoss ? ControlledBoss->GetCurrentPhase() : EBossPhase::Ground;
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
	return ControlledBoss ? ControlledBoss->IsInFinisherPhase() : false;
}
