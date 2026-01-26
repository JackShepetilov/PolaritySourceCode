// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CheckpointData.h"
#include "CheckpointSubsystem.generated.h"

class ACheckpointActor;
class AShooterCharacter;
class AShooterNPC;

/**
 * Stores data needed to respawn an NPC.
 */
USTRUCT()
struct FNPCSpawnData
{
	GENERATED_BODY()

	/** Class of the NPC to spawn */
	UPROPERTY()
	TSubclassOf<AShooterNPC> NPCClass;

	/** Transform where NPC should spawn */
	UPROPERTY()
	FTransform SpawnTransform;

	/** Unique ID to track this NPC instance */
	UPROPERTY()
	FGuid SpawnID;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCheckpointActivated, const FCheckpointData&, CheckpointData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerRespawned);

/**
 * World Subsystem that manages checkpoint system for the current level.
 * Handles checkpoint registration, activation, and player respawning.
 *
 * Session-based: checkpoint data is NOT persisted between game sessions.
 */
UCLASS()
class POLARITY_API UCheckpointSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * Register a checkpoint actor with the subsystem.
	 * Called automatically by ACheckpointActor on BeginPlay.
	 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	void RegisterCheckpoint(ACheckpointActor* Checkpoint);

	/**
	 * Unregister a checkpoint actor.
	 * Called automatically by ACheckpointActor on EndPlay.
	 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	void UnregisterCheckpoint(ACheckpointActor* Checkpoint);

	/**
	 * Activate a checkpoint for the given character.
	 * Saves player state and sets this as the active respawn point.
	 *
	 * @param Checkpoint The checkpoint actor being activated
	 * @param Character The character activating the checkpoint
	 * @return True if checkpoint was successfully activated
	 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	bool ActivateCheckpoint(ACheckpointActor* Checkpoint, AShooterCharacter* Character);

	/**
	 * Respawn the character at the last activated checkpoint.
	 * Teleports character and restores saved state.
	 *
	 * @param Character The character to respawn
	 * @return True if respawn was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	bool RespawnAtCheckpoint(AShooterCharacter* Character);

	/**
	 * Check if there's a valid checkpoint to respawn at.
	 */
	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	bool HasActiveCheckpoint() const { return CurrentCheckpointData.bIsValid; }

	/**
	 * Get the current checkpoint data (read-only).
	 */
	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	const FCheckpointData& GetCurrentCheckpointData() const { return CurrentCheckpointData; }

	/**
	 * Clear all checkpoint data (e.g., on level restart).
	 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	void ClearCheckpointData();

	/**
	 * Mark a sequence as completed (for skip functionality).
	 * @param SequenceName Unique identifier for the sequence
	 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint|Sequences")
	void MarkSequenceCompleted(FName SequenceName);

	/**
	 * Check if a sequence was completed before current checkpoint.
	 * Use this to skip cutscenes/intros on respawn.
	 * @param SequenceName Unique identifier for the sequence
	 * @return True if sequence should be skipped
	 */
	UFUNCTION(BlueprintPure, Category = "Checkpoint|Sequences")
	bool ShouldSkipSequence(FName SequenceName) const;

	/** Broadcast when a checkpoint is activated */
	UPROPERTY(BlueprintAssignable, Category = "Checkpoint|Events")
	FOnCheckpointActivated OnCheckpointActivated;

	/** Broadcast when player respawns at checkpoint */
	UPROPERTY(BlueprintAssignable, Category = "Checkpoint|Events")
	FOnPlayerRespawned OnPlayerRespawned;

	// ==================== NPC Respawn System ====================

	/**
	 * Register an NPC for checkpoint tracking.
	 * Called by NPC on BeginPlay. Stores spawn data for potential respawn.
	 * @param NPC The NPC to register
	 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint|NPC")
	void RegisterNPC(AShooterNPC* NPC);

	/**
	 * Notify that an NPC has died.
	 * NPC will be respawned if player dies and returns to checkpoint.
	 * @param NPC The NPC that died
	 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint|NPC")
	void NotifyNPCDeath(AShooterNPC* NPC);

protected:
	/** Currently active checkpoint data */
	UPROPERTY()
	FCheckpointData CurrentCheckpointData;

	/** All registered checkpoints in the level */
	UPROPERTY()
	TArray<TWeakObjectPtr<ACheckpointActor>> RegisteredCheckpoints;

	/** Sequences completed during this session (persists across respawns) */
	UPROPERTY()
	TSet<FName> SessionCompletedSequences;

	// ==================== NPC Tracking ====================

	/** Map of registered NPCs: SpawnID -> SpawnData */
	UPROPERTY()
	TMap<FGuid, FNPCSpawnData> RegisteredNPCs;

	/** NPCs killed after the last checkpoint activation (will be respawned on player death) */
	UPROPERTY()
	TArray<FGuid> NPCsKilledAfterCheckpoint;

	/** NPCs that are currently alive (tracked for respawn) */
	UPROPERTY()
	TArray<TWeakObjectPtr<AShooterNPC>> AliveNPCs;

	/** Respawn all NPCs to checkpoint state (destroy survivors, respawn killed) */
	void RespawnAllNPCsToCheckpointState();
};
