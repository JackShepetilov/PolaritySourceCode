// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "CheckpointSubsystem.h"
#include "CheckpointActor.h"
#include "Polarity/Variant_Shooter/ShooterCharacter.h"
#include "Polarity/Variant_Shooter/AI/ShooterNPC.h"
#include "Polarity/Variant_Shooter/AI/ShooterAIController.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Components/StateTreeAIComponent.h"

void UCheckpointSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CurrentCheckpointData.Invalidate();
	SessionCompletedSequences.Empty();
}

void UCheckpointSubsystem::Deinitialize()
{
	ClearCheckpointData();
	RegisteredCheckpoints.Empty();
	SessionCompletedSequences.Empty();
	RegisteredNPCs.Empty();
	NPCsAliveAtCheckpoint.Empty();
	AliveNPCs.Empty();
	Super::Deinitialize();
}

bool UCheckpointSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Create for all game worlds, not for editor preview worlds
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UCheckpointSubsystem::RegisterCheckpoint(ACheckpointActor* Checkpoint)
{
	if (!IsValid(Checkpoint))
	{
		return;
	}

	// Avoid duplicates
	for (const TWeakObjectPtr<ACheckpointActor>& Existing : RegisteredCheckpoints)
	{
		if (Existing.Get() == Checkpoint)
		{
			return;
		}
	}

	RegisteredCheckpoints.Add(Checkpoint);
}

void UCheckpointSubsystem::UnregisterCheckpoint(ACheckpointActor* Checkpoint)
{
	RegisteredCheckpoints.RemoveAll([Checkpoint](const TWeakObjectPtr<ACheckpointActor>& Ptr)
	{
		return !Ptr.IsValid() || Ptr.Get() == Checkpoint;
	});
}

bool UCheckpointSubsystem::ActivateCheckpoint(ACheckpointActor* Checkpoint, AShooterCharacter* Character)
{
	if (!IsValid(Checkpoint) || !IsValid(Character))
	{
		return false;
	}

	// Save character state to checkpoint data
	FCheckpointData NewData;
	NewData.SpawnTransform = Checkpoint->GetSpawnTransform();
	NewData.CheckpointID = Checkpoint->GetCheckpointID();
	NewData.bIsValid = true;

	// Get state from character
	if (!Character->SaveToCheckpoint(NewData))
	{
		return false;
	}

	// Copy session-completed sequences to checkpoint data
	for (const FName& SequenceName : SessionCompletedSequences)
	{
		NewData.CompletedSequences.Add(SequenceName, true);
	}

	CurrentCheckpointData = NewData;

	// Snapshot all currently alive NPCs - these will be respawned if player dies
	NPCsAliveAtCheckpoint.Empty();
	for (const TWeakObjectPtr<AShooterNPC>& NPCPtr : AliveNPCs)
	{
		if (AShooterNPC* NPC = NPCPtr.Get())
		{
			FGuid SpawnID = NPC->GetCheckpointSpawnID();
			if (SpawnID.IsValid())
			{
				NPCsAliveAtCheckpoint.Add(SpawnID);
			}
		}
	}

	OnCheckpointActivated.Broadcast(CurrentCheckpointData);

	return true;
}

bool UCheckpointSubsystem::RespawnAtCheckpoint(AShooterCharacter* Character)
{
	if (!HasActiveCheckpoint())
	{
		UE_LOG(LogTemp, Warning, TEXT("CheckpointSubsystem: No active checkpoint for respawn"));
		return false;
	}

	if (!IsValid(Character))
	{
		UE_LOG(LogTemp, Warning, TEXT("CheckpointSubsystem: Invalid character for respawn"));
		return false;
	}

	// Respawn all NPCs to checkpoint state FIRST (before player restore)
	// This ensures NPCs are reset before they can target the respawning player
	RespawnAllNPCsToCheckpointState();

	// Restore character state from checkpoint
	if (!Character->RestoreFromCheckpoint(CurrentCheckpointData))
	{
		UE_LOG(LogTemp, Error, TEXT("CheckpointSubsystem: Failed to restore character from checkpoint"));
		return false;
	}

	OnPlayerRespawned.Broadcast();
	return true;
}

void UCheckpointSubsystem::SetCheckpointData(const FCheckpointData& NewData)
{
	CurrentCheckpointData = NewData;

	// Snapshot alive NPCs for potential respawn
	NPCsAliveAtCheckpoint.Empty();
	for (const TWeakObjectPtr<AShooterNPC>& NPCPtr : AliveNPCs)
	{
		if (AShooterNPC* NPC = NPCPtr.Get())
		{
			FGuid SpawnID = NPC->GetCheckpointSpawnID();
			if (SpawnID.IsValid())
			{
				NPCsAliveAtCheckpoint.Add(SpawnID);
			}
		}
	}

	OnCheckpointActivated.Broadcast(CurrentCheckpointData);
}

void UCheckpointSubsystem::ClearCheckpointData()
{
	CurrentCheckpointData.Invalidate();
	// Note: We don't clear SessionCompletedSequences here
	// as they should persist for the entire level session
}

void UCheckpointSubsystem::MarkSequenceCompleted(FName SequenceName)
{
	if (!SequenceName.IsNone())
	{
		SessionCompletedSequences.Add(SequenceName);
	}
}

bool UCheckpointSubsystem::ShouldSkipSequence(FName SequenceName) const
{
	// Check both current checkpoint data and session data
	if (CurrentCheckpointData.bIsValid && CurrentCheckpointData.CompletedSequences.Contains(SequenceName))
	{
		return true;
	}
	return SessionCompletedSequences.Contains(SequenceName);
}

void UCheckpointSubsystem::RegisterNPC(AShooterNPC* NPC)
{
	if (!IsValid(NPC))
	{
		return;
	}

	// Check if this NPC already has a SpawnID (respawned NPC)
	FGuid ExistingID = NPC->GetCheckpointSpawnID();
	if (ExistingID.IsValid())
	{
		// Just track as alive, don't create new spawn data
		AliveNPCs.Add(NPC);
		return;
	}

	// Create spawn data for new NPC
	FNPCSpawnData SpawnData;
	SpawnData.NPCClass = NPC->GetClass();
	SpawnData.SpawnTransform = NPC->GetActorTransform();
	SpawnData.SpawnID = FGuid::NewGuid();

	// Store on the NPC for later reference
	NPC->SetCheckpointSpawnID(SpawnData.SpawnID);

	// Register spawn data and track as alive
	RegisteredNPCs.Add(SpawnData.SpawnID, SpawnData);
	AliveNPCs.Add(NPC);
}

void UCheckpointSubsystem::NotifyNPCDeath(AShooterNPC* NPC)
{
	if (!IsValid(NPC))
	{
		return;
	}

	// Remove from alive list
	AliveNPCs.RemoveAll([NPC](const TWeakObjectPtr<AShooterNPC>& Ptr)
	{
		return !Ptr.IsValid() || Ptr.Get() == NPC;
	});
}

void UCheckpointSubsystem::RespawnAllNPCsToCheckpointState()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Step 1: Destroy all currently alive NPCs (with proper controller/StateTree cleanup)
	for (const TWeakObjectPtr<AShooterNPC>& NPCPtr : AliveNPCs)
	{
		if (AShooterNPC* NPC = NPCPtr.Get())
		{
			// Get the controller and clean it up properly before destroying NPC
			if (AShooterAIController* AIController = Cast<AShooterAIController>(NPC->GetController()))
			{
				// Stop StateTree first to clean up debug tags and state
				if (UStateTreeAIComponent* StateTreeComp = AIController->FindComponentByClass<UStateTreeAIComponent>())
				{
					StateTreeComp->StopLogic(TEXT("CheckpointRespawn"));
				}
				AIController->UnPossess();
				AIController->Destroy();
			}
			NPC->Destroy();
		}
	}
	AliveNPCs.Empty();

	// Step 2: Respawn ALL NPCs that were alive at checkpoint activation
	// Use SpawnAIFromClass to properly initialize AI controller and StateTree
	for (const FGuid& SpawnID : NPCsAliveAtCheckpoint)
	{
		if (const FNPCSpawnData* SpawnData = RegisteredNPCs.Find(SpawnID))
		{
			if (SpawnData->NPCClass)
			{
				APawn* SpawnedPawn = UAIBlueprintHelperLibrary::SpawnAIFromClass(
					World,
					SpawnData->NPCClass,
					nullptr, // No BehaviorTree - we use StateTree configured on the controller
					SpawnData->SpawnTransform.GetLocation(),
					SpawnData->SpawnTransform.Rotator(),
					true // bNoCollisionFail
				);

				if (AShooterNPC* NewNPC = Cast<AShooterNPC>(SpawnedPawn))
				{
					NewNPC->SetCheckpointSpawnID(SpawnID);
					AliveNPCs.Add(NewNPC);

					// Force perception update so NPC detects player immediately
					if (AShooterAIController* AIController = Cast<AShooterAIController>(NewNPC->GetController()))
					{
						AIController->ForcePerceptionUpdate();
					}
				}
			}
		}
	}
}
