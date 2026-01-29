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
	// Debug: Log all perception updates with sense type
	const bool bIsSight = Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>();
	const bool bIsTeam = Stimulus.Type == UAISense::GetSenseID<UAISense_Team>();

	UE_LOG(LogTemp, Log, TEXT("[%s] OnPerceptionUpdated: Actor=%s, Sensed=%d, SenseType=%s (Sight=%d, Team=%d)"),
		*GetName(),
		Actor ? *Actor->GetName() : TEXT("NULL"),
		Stimulus.WasSuccessfullySensed(),
		*Stimulus.Type.Name.ToString(),
		bIsSight,
		bIsTeam);

	// If this is a Team sense event, broadcast to Blueprint
	if (bIsTeam && Stimulus.WasSuccessfullySensed())
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] >>> RECEIVED TEAM PERCEPTION about %s at location %s"),
			*GetName(),
			Actor ? *Actor->GetName() : TEXT("NULL"),
			*Stimulus.StimulusLocation.ToString());

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

	// Debug: Count nearby teammates and check their perception configs
	int32 NearbyTeammates = 0;
	for (TActorIterator<AShooterAIController> It(GetWorld()); It; ++It)
	{
		AShooterAIController* OtherController = *It;
		if (OtherController && OtherController != this && OtherController->GetPawn())
		{
			if (OtherController->GetGenericTeamId() == GetGenericTeamId())
			{
				float Distance = FVector::Dist(GetPawn()->GetActorLocation(), OtherController->GetPawn()->GetActorLocation());
				if (Distance <= TeamPerceptionRadius)
				{
					NearbyTeammates++;

					// Check if teammate has Team sense configured
					bool bHasTeamSense = false;
					if (OtherController->AIPerception)
					{
						FAISenseID TeamSenseID = UAISense::GetSenseID<UAISense_Team>();
						bHasTeamSense = OtherController->AIPerception->GetSenseConfig(TeamSenseID) != nullptr;
					}

					UE_LOG(LogTemp, Warning, TEXT("  -> Teammate %s (dist=%.0f, TeamSenseConfigured=%d)"),
						*OtherController->GetName(),
						Distance,
						bHasTeamSense);
				}
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[%s] <<< BROADCASTING: enemy=%s, radius=%.0f, NearbyTeammates=%d, BroadcasterLoc=%s"),
		*GetName(),
		*DetectedEnemy->GetName(),
		TeamPerceptionRadius,
		NearbyTeammates,
		*GetPawn()->GetActorLocation().ToString());
}