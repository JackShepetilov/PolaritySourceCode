// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ShooterGameMode.generated.h"

class UShooterUI;
class AShooterCharacter;
class UUserWidget;
class ARunLaunchPoint;

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

	/** If true, the sea-toss launch fires automatically when the world is ready. Set false to
	 *  trigger it from BP (e.g. timed with the intro Level Sequence) via PerformRunLaunch(). */
	UPROPERTY(EditAnywhere, Category = "Run Start")
	bool bAutoLaunchOnReady = true;

	/** Ensures the run is started exactly once. */
	bool bRunStartTriggered = false;

	/** Handle for the first-rendered-frame hook. */
	FDelegateHandle ViewportRenderedHandle;

	/** Latched on the first actually-rendered frame after the level loads. */
	void OnFirstViewportRendered();

	/** Called once the world is confirmed loaded and drawn; starts the run. */
	void HandleWorldReady();

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

	/** Fired right after the run starts; implement in BP to play the intro Level Sequence + launch. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Run Start")
	void BP_OnRunStartReady();
};
