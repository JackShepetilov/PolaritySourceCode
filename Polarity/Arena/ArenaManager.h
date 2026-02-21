// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaWaveData.h"
#include "ArenaManager.generated.h"

class UBoxComponent;
class AArenaSpawnPoint;
class AShooterNPC;
class AFlyingDrone;
class AShooterCharacter;
class UCheckpointSubsystem;
class AShooterDoor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnArenaStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnArenaCleared);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWaveStarted, int32, WaveIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWaveCleared, int32, WaveIndex);

/**
 * Manages a combat arena: activation, wave spawning, exit blockers, and checkpoint integration.
 *
 * Place one per arena level. Configure waves in the Details panel.
 * Blockers are separate mesh actors referenced by this manager.
 * Activation happens when the player overlaps a blocker and stays inside.
 */
UCLASS(Blueprintable)
class POLARITY_API AArenaManager : public AActor
{
	GENERATED_BODY()

public:
	AArenaManager();

	// ==================== Wave Configuration ====================

	/** Waves of enemies to spawn, in order */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Waves")
	TArray<FArenaWave> Waves;

	/** Pause between waves (seconds). Next wave auto-starts after this delay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Waves", meta = (ClampMin = "0.0"))
	float TimeBetweenWaves = 3.0f;

	// ==================== Blockers ====================

	/**
	 * Actors that block arena exits during combat (mesh walls/doors).
	 * Collision + visibility are toggled by the manager.
	 * One of these also serves as the entry trigger (first overlapped).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Blockers")
	TArray<TSoftObjectPtr<AActor>> ExitBlockers;

	// ==================== Spawn Points ====================

	/** Spawn point markers placed around the arena */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Spawn")
	TArray<TSoftObjectPtr<AArenaSpawnPoint>> SpawnPoints;

	// ==================== Respawn ====================

	/** Where the player respawns if they die during this arena fight */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Respawn")
	TSoftObjectPtr<AActor> PlayerRespawnPoint;

	// ==================== Reward Door ====================

	/** Door that opens when all waves are cleared (e.g. upgrade room) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Door")
	TSoftObjectPtr<AShooterDoor> RewardDoor;

	// ==================== State (Read-Only) ====================

	/** Current arena state */
	UPROPERTY(BlueprintReadOnly, Category = "Arena|State")
	EArenaState CurrentState = EArenaState::Idle;

	/** Current wave index (0-based) */
	UPROPERTY(BlueprintReadOnly, Category = "Arena|State")
	int32 CurrentWaveIndex = -1;

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "Arena|Events")
	FOnArenaStarted OnArenaStarted;

	UPROPERTY(BlueprintAssignable, Category = "Arena|Events")
	FOnArenaCleared OnArenaCleared;

	UPROPERTY(BlueprintAssignable, Category = "Arena|Events")
	FOnWaveStarted OnWaveStarted;

	UPROPERTY(BlueprintAssignable, Category = "Arena|Events")
	FOnWaveCleared OnWaveCleared;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	// ==================== Activation ====================

	/** Called when player touches the invisible blocker shell */
	UFUNCTION()
	void OnBlockerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Called after delay — player has passed through, now activate */
	void OnActivationDelayFinished();

	/** Activate the arena: close blockers, save checkpoint, start wave 0 */
	void ActivateArena(AShooterCharacter* Player);

	// ==================== Blockers ====================

	/** Set blocker visibility and collision */
	void SetBlockersActive(bool bActive);

	/** Register overlap callbacks on blocker meshes */
	void RegisterBlockerOverlaps();

	// ==================== Wave Spawning ====================

	/** Spawn all NPCs for the given wave index */
	void SpawnWave(int32 WaveIndex);

	/** Pick a random spawn point appropriate for the NPC class, avoiding already used points */
	AArenaSpawnPoint* PickSpawnPoint(TSubclassOf<AShooterNPC> NPCClass, const TArray<AArenaSpawnPoint*>& UsedPoints) const;

	/** Called when a spawned NPC dies */
	UFUNCTION()
	void OnNPCDied(AShooterNPC* DeadNPC);

	/** Check if current wave is cleared and advance */
	void CheckWaveComplete();

	/** Timer callback to start the next wave */
	void StartNextWave();

	// ==================== Completion ====================

	/** All waves cleared — open blockers */
	void CompleteArena();

	// ==================== Reset ====================

	/** Full reset: destroy NPCs, hide blockers, return to Idle */
	void ResetArena();

	/** Called when player respawns from checkpoint */
	UFUNCTION()
	void OnPlayerRespawned();

	// ==================== Checkpoint ====================

	/** Override checkpoint spawn transform to our respawn point */
	void SaveArenaCheckpoint(AShooterCharacter* Player);

	// ==================== Runtime Data ====================

	/** NPCs currently alive in the active wave */
	UPROPERTY()
	TArray<TWeakObjectPtr<AShooterNPC>> AliveNPCs;

	/** Timer for between-wave delay */
	FTimerHandle WaveTimerHandle;

	/** Timer for activation delay (player passing through blocker) */
	FTimerHandle ActivationDelayHandle;

	/** Player who touched the blocker, waiting for delay */
	TWeakObjectPtr<AShooterCharacter> PendingPlayer;

	/** Cached reference to checkpoint subsystem */
	UPROPERTY()
	TObjectPtr<UCheckpointSubsystem> CheckpointSubsystem;

	/** Whether we already bound to OnPlayerRespawned delegate */
	bool bBoundToRespawn = false;
};
