// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/AI/ShooterAIController.h"
#include "ShooterNPC.h"
#include "Components/StateTreeAIComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "AI/Navigation/PathFollowingAgentInterface.h"

AShooterAIController::AShooterAIController()
{
	// create the StateTree component
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));

	// create the AI perception component. It will be configured in BP
	AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));

	// subscribe to the AI perception delegates
	AIPerception->OnTargetPerceptionUpdated.AddDynamic(this, &AShooterAIController::OnPerceptionUpdated);
	AIPerception->OnTargetPerceptionForgotten.AddDynamic(this, &AShooterAIController::OnPerceptionForgotten);
}

void AShooterAIController::BeginPlay()
{
	Super::BeginPlay();

	// Debug StateTree status after BeginPlay (when StateTree should be running)
	FString StateTreeStatus = TEXT("NO COMPONENT");
	if (StateTreeAI)
	{
		const bool bIsRunning = StateTreeAI->IsRunning();
		StateTreeStatus = bIsRunning ? TEXT("RUNNING") : TEXT("NOT RUNNING");
	}
	UE_LOG(LogTemp, Warning, TEXT("[%s] BeginPlay - Pawn=%s - StateTreeAI: %s"),
		*GetName(),
		GetPawn() ? *GetPawn()->GetName() : TEXT("NULL"),
		*StateTreeStatus);
}

void AShooterAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// ensure we're possessing an NPC
	if (AShooterNPC* NPC = Cast<AShooterNPC>(InPawn))
	{
		// add the team tag to the pawn
		NPC->Tags.Add(TeamTag);

		// subscribe to the pawn's OnDeath delegate
		NPC->OnNPCDeath.AddDynamic(this, &AShooterAIController::OnPawnDeath);
	}
}

void AShooterAIController::OnPawnDeath(AShooterNPC* DeadNPC)
{
	// stop movement
	GetPathFollowingComponent()->AbortMove(*this, FPathFollowingResultFlags::UserAbort);

	// stop StateTree logic
	StateTreeAI->StopLogic(FString(""));

	// unpossess the pawn
	UnPossess();

	// destroy this controller
	Destroy();
}

void AShooterAIController::SetCurrentTarget(AActor* Target)
{
	TargetEnemy = Target;
	SetFocus(Target);
}

void AShooterAIController::ClearCurrentTarget()
{
	TargetEnemy = nullptr;
	ClearFocus(EAIFocusPriority::Gameplay);
}

void AShooterAIController::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	// pass the data to the StateTree delegate hook
	OnShooterPerceptionUpdated.ExecuteIfBound(Actor, Stimulus);
}

void AShooterAIController::OnPerceptionForgotten(AActor* Actor)
{
	// pass the data to the StateTree delegate hook
	OnShooterPerceptionForgotten.ExecuteIfBound(Actor);
}