// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ShooterGameMode.generated.h"

class UShooterUI;
class AShooterCharacter;

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
};
