// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/AI/ShooterAIController.h"
#include "ShooterNPC.h"
#include "Components/StateTreeAIComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense_Team.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig.h"
#include "Navigation/PathFollowingComponent.h"
#include "EngineUtils.h"  // For TActorIterator
#include "AI/Navigation/PathFollowingAgentInterface.h"
#include "AI/Navigation/PolarityPathFollowingComponent.h"

AShooterAIController::AShooterAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UPolarityPathFollowingComponent>(TEXT("PathFollowingComponent")))
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

	// Debug: Check if Team Sense is configured in perception
	FString TeamSenseStatus = TEXT("NOT CONFIGURED");
	if (AIPerception)
	{
		// Check if Team sense is registered
		FAISenseID TeamSenseID = UAISense::GetSenseID<UAISense_Team>();
		if (TeamSenseID.IsValid())
		{
			// Check if this perception component is listening for team sense
			const UAISenseConfig* TeamConfig = AIPerception->GetSenseConfig(TeamSenseID);
			if (TeamConfig)
			{
				TeamSenseStatus = TEXT("CONFIGURED");
			}
			else
			{
				TeamSenseStatus = TEXT("SENSE EXISTS BUT NOT IN CONFIG - Add AISenseConfig_Team to AIPerception!");
			}
		}
		else
		{
			TeamSenseStatus = TEXT("SENSE ID INVALID");
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[%s] BeginPlay - Pawn=%s - StateTreeAI: %s - TeamSense: %s"),
		*GetName(),
		GetPawn() ? *GetPawn()->GetName() : TEXT("NULL"),
		*StateTreeStatus,
		*TeamSenseStatus);
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

		// Ensure StateTree is running (may not auto-start after dynamic spawn)
		if (StateTreeAI && !StateTreeAI->IsRunning())
		{
			StateTreeAI->StartLogic();
		}

		// Force perception update on possess (needed for checkpoint respawn)
		ForcePerceptionUpdate();
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
	// The lock, and the one place it can be enforced. Perception calls this from three sites (the
	// stimulus lambda, the known-actor sweep on entry and the periodic poll), the arena calls it, and
	// damage retaliation calls it; gating each of them separately would leave whichever one was
	// missed quietly cancelling the decoy.
	if (IsDistracted() && Target != Distraction.Get())
	{
		return;
	}

	TargetEnemy = Target;
	SetFocus(Target);
}

void AShooterAIController::ClearCurrentTarget()
{
	// Same reason as above. The distraction is what ends the distraction — and when the decoy is
	// destroyed IsDistracted() is already false, so a clear arriving then goes through and the NPC
	// picks somebody up again on the next perception update.
	if (IsDistracted())
	{
		return;
	}

	TargetEnemy = nullptr;
	ClearFocus(EAIFocusPriority::Gameplay);
}

void AShooterAIController::DistractTo(AActor* Decoy, float Seconds)
{
	if (!Decoy || !GetWorld())
	{
		return;
	}

	const bool bIsNew = Distraction.Get() != Decoy;

	Distraction = Decoy;
	DistractionEndTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, Seconds);

	// Straight to the field, not through SetCurrentTarget: that one now refuses anything other than
	// the distraction, and the distraction is what this is.
	TargetEnemy = Decoy;
	SetFocus(Decoy);

	if (bIsNew)
	{
		UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s distracted by %s for %.1fs"),
			*GetNameSafe(GetPawn()), *Decoy->GetName(), Seconds);
	}
}

bool AShooterAIController::IsDistracted() const
{
	if (!Distraction.IsValid() || !GetWorld())
	{
		return false;
	}
	return GetWorld()->GetTimeSeconds() < DistractionEndTime;
}

AActor* AShooterAIController::GetDistraction() const
{
	return Distraction.Get();
}

void AShooterAIController::EndDistraction()
{
	AActor* Previous = Distraction.Get();

	Distraction.Reset();
	DistractionEndTime = 0.0f;

	if (!Previous)
	{
		return;
	}

	// Let go of the decoy as a target as well, and this is not optional. A spent decoy is an ordinary
	// prop sitting on the floor, and the NPC would happily keep shooting it forever: perception only
	// looks for somebody new while the sense task has NO target, so a stale but still valid one is
	// never replaced. Clearing it puts the NPC back in the "looking for someone" state, and the
	// perception poll finds a player within half a second.
	if (TargetEnemy == Previous)
	{
		TargetEnemy = nullptr;
		ClearFocus(EAIFocusPriority::Gameplay);
	}

	UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s stops being distracted by %s"),
		*GetNameSafe(GetPawn()), *Previous->GetName());
}

void AShooterAIController::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	const bool bIsSight = Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>();
	const bool bIsTeam = Stimulus.Type == UAISense::GetSenseID<UAISense_Team>();

	// If this is a Team sense event, broadcast to Blueprint
	if (bIsTeam && Stimulus.WasSuccessfullySensed())
	{
		// Broadcast Blueprint event for team perception
		OnTeamPerceptionReceived.Broadcast(Actor, Stimulus.StimulusLocation);
	}

	// If this is a Sight sense event, broadcast appropriate Blueprint events
	if (bIsSight)
	{
		if (Stimulus.WasSuccessfullySensed())
		{
			// Enemy spotted via sight
			OnEnemySpotted.Broadcast(Actor, Stimulus.StimulusLocation);
		}
		else
		{
			// Enemy lost (sight sense returned false = no longer visible)
			OnEnemyLost.Broadcast(Actor);
		}
	}

	// pass the data to the StateTree delegate hook
	OnShooterPerceptionUpdated.ExecuteIfBound(Actor, Stimulus);

	// If we successfully detected an enemy, broadcast to teammates
	if (bSharePerceptionWithTeam && Stimulus.WasSuccessfullySensed() && Actor)
	{
		// Only broadcast sight sense detections to avoid spam (don't re-broadcast team events)
		if (bIsSight)
		{
			BroadcastEnemyToTeam(Actor, Actor->GetActorLocation());
		}
	}
}

void AShooterAIController::OnPerceptionForgotten(AActor* Actor)
{
	// Broadcast Blueprint event
	OnEnemyLost.Broadcast(Actor);

	// pass the data to the StateTree delegate hook
	OnShooterPerceptionForgotten.ExecuteIfBound(Actor);
}

void AShooterAIController::ForcePerceptionUpdate()
{
	if (AIPerception)
	{
		AIPerception->RequestStimuliListenerUpdate();
	}
}

void AShooterAIController::BroadcastEnemyToTeam(AActor* DetectedEnemy, const FVector& LastKnownLocation)
{
	if (!DetectedEnemy || !GetPawn())
	{
		return;
	}

	UAIPerceptionSystem* PerceptionSystem = UAIPerceptionSystem::GetCurrent(GetWorld());
	if (!PerceptionSystem)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s] BroadcastEnemyToTeam: No PerceptionSystem!"), *GetName());
		return;
	}

	// Create team stimulus event
	FAITeamStimulusEvent TeamEvent(
		GetPawn(),              // Broadcaster - our pawn
		DetectedEnemy,          // Enemy - the detected actor
		LastKnownLocation,      // Where we saw them
		TeamPerceptionRadius,   // How far to broadcast (radius)
		0.0f,                   // Info age - 0 means fresh info
		1.0f                    // Strength - 1.0 = high confidence
	);

	// Send the event to the perception system (this internally calls UAISense_Team::RegisterEvent)
	PerceptionSystem->OnEvent(TeamEvent);

}