// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ShooterGameMode.generated.h"

class UShooterUI;
class AShooterCharacter;
class UUserWidget;
class ARunLaunchPoint;
class FViewport;

/**
 *  Simple GameMode for a first person shooter game
 *  Manages game UI, team scores, and checkpoint respawning
 */
UCLASS(abstract)
class POLARITY_API AShooterGameMode : public AGameModeBase
{
	GENERATED_BODY()
	
protected:

	/** Type of UI widget to spawn */
	UPROPERTY(EditAnywhere, Category="Shooter")
	TSubclassOf<UShooterUI> ShooterUIClass;

	/** Pointer to the UI widget */
	TObjectPtr<UShooterUI> ShooterUI;

	/** Map of scores by team ID */
	TMap<uint8, int32> TeamScores;

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Cleans up the run-start gate's viewport delegate */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ===== Run-start loading gate =====

	/** Full-screen black widget shown from the first frame until the intro reveals the world. Assign in BP. */
	UPROPERTY(EditAnywhere, Category = "Run Start")
	TSubclassOf<UUserWidget> LoadingCoverClass;

	/** Live instance of the loading cover. */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> LoadingCoverWidget;

	/** The run-map marker found at BeginPlay; null on non-run maps (then the gate stays idle). */
	UPROPERTY(Transient)
	TObjectPtr<ARunLaunchPoint> RunMarker;

	/** Ensures the run is started exactly once. */
	bool bRunStartTriggered = false;

	/** Handle for the first-rendered-frame hook. */
	FDelegateHandle ViewportRenderedHandle;

	/** Latched on the first actually-rendered frame after the level loads. (UE5.6 passes the FViewport*.) */
	void OnFirstViewportRendered(FViewport* Viewport);

	/** Called once the world is confirmed loaded and drawn; starts the run. */
	void HandleWorldReady();

	/** Finds the run marker and arms the gate. Retries across ticks while streaming
	 *  sublevels are still being added: in standalone/packaged, always-loaded sublevels
	 *  AddToWorld AFTER world BeginPlay (time-sliced), so a marker living in a sublevel
	 *  is not findable on the first tick — PIE hides this because the duplicated editor
	 *  world is already fully composed. */
	void TryInitRunGate();

	/** Creates and shows the black loading cover once (idempotent). */
	void EnsureLoadingCover();

	/** Teleports the player to the level's ARunLaunchPoint and tosses them into the air. */
	UFUNCTION(BlueprintCallable, Category = "Run Start")
	void PerformRunLaunch();

public:

	/** Increases the score for the given team */
	void IncrementTeamScore(uint8 TeamByte);

	/**
	 * Respawn player at last checkpoint (called from Pause Menu).
	 * If no checkpoint exists, restarts the level.
	 * @param PlayerController The player requesting respawn
	 * @return True if respawn was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	bool RespawnPlayerAtCheckpoint(APlayerController* PlayerController);

	/**
	 * Check if a checkpoint is available for respawn.
	 * @return True if there's a valid checkpoint to respawn at
	 */
	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	bool HasCheckpointAvailable() const;

	/**
	 * Restart the current level from the beginning.
	 * Clears all checkpoint data.
	 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	void RestartLevel();

	/** Removes the black loading cover. Call from BP once the intro fade has revealed the world. */
	UFUNCTION(BlueprintCallable, Category = "Run Start")
	void DismissLoadingCover();

	/** Fired once the world is loaded & the first frame is drawn (run maps only). Implement in BP to run
	 *  the run-start sequence: stream overlay; if !RunSubsystem.IsRunActive -> set configs + StartRun;
	 *  EnterArena(ArenaIndex). Then branch on the intro type:
	 *   - bLaunchFromSea -> PerformRunLaunch() (weapon is granted on landing);
	 *   - bBossIntro -> lock input, play the boss intro Level Sequence, on finish blend the camera back
	 *     to the player, EnableInput, EquipStartingWeaponAnimated(), then ForceActivateArena() to aggro;
	 *   - otherwise -> normal spawn. Finally fade in + DismissLoadingCover(). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Run Start")
	void BP_OnRunStartReady(int32 ArenaIndex, bool bLaunchFromSea, bool bBossIntro);
};
