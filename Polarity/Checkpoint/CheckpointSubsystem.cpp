// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "CheckpointSubsystem.h"
#include "CheckpointActor.h"
#include "Polarity/Variant_Shooter/ShooterCharacter.h"

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

	// Restore character state from checkpoint
	if (!Character->RestoreFromCheckpoint(CurrentCheckpointData))
	{
		UE_LOG(LogTemp, Error, TEXT("CheckpointSubsystem: Failed to restore character from checkpoint"));
		return false;
	}

	OnPlayerRespawned.Broadcast();
	return true;
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
