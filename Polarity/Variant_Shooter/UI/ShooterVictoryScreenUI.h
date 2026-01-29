// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterVictoryScreenUI.generated.h"

/**
 * Victory/Level Complete Screen UI widget for the shooter game.
 * Shown when player completes a level or wins the game.
 */
UCLASS(abstract)
class POLARITY_API UShooterVictoryScreenUI : public UUserWidget
{
	GENERATED_BODY()

public:

	// ==================== Victory State ====================

	/**
	 * Called when the victory screen is shown.
	 * @param LevelName - name of the completed level
	 * @param bFinalLevel - true if this was the final level
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|VictoryScreen", meta = (DisplayName = "OnVictoryScreenShown"))
	void BP_OnVictoryScreenShown(const FString& LevelName, bool bFinalLevel);

	// ==================== Stats Display ====================

	/**
	 * Updates level completion stats.
	 * @param TotalKills - total enemies killed
	 * @param TotalDeaths - total player deaths
	 * @param CompletionTime - level completion time in seconds
	 * @param AccuracyPercent - shooting accuracy (0-100)
	 * @param HeadshotPercent - headshot percentage (0-100)
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|VictoryScreen|Stats", meta = (DisplayName = "UpdateVictoryStats"))
	void BP_UpdateVictoryStats(int32 TotalKills, int32 TotalDeaths, float CompletionTime, float AccuracyPercent, float HeadshotPercent);

	/**
	 * Updates score/ranking display.
	 * @param Score - final score for the level
	 * @param Rank - letter rank (S, A, B, C, D, F)
	 * @param HighScore - previous high score (0 if this is first completion)
	 * @param bIsNewHighScore - true if current score beats high score
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|VictoryScreen|Stats", meta = (DisplayName = "UpdateScoreDisplay"))
	void BP_UpdateScoreDisplay(int32 Score, const FString& Rank, int32 HighScore, bool bIsNewHighScore);

	/**
	 * Updates challenge/objective completion.
	 * @param CompletedObjectives - number of objectives completed
	 * @param TotalObjectives - total objectives in level
	 * @param BonusObjectivesCompleted - number of optional/bonus objectives completed
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|VictoryScreen|Stats", meta = (DisplayName = "UpdateObjectivesDisplay"))
	void BP_UpdateObjectivesDisplay(int32 CompletedObjectives, int32 TotalObjectives, int32 BonusObjectivesCompleted);

	// ==================== Actions (called from Blueprint) ====================

	/** Continue to next level */
	UFUNCTION(BlueprintCallable, Category = "Shooter|VictoryScreen")
	void ContinueToNextLevel();

	/** Replay current level */
	UFUNCTION(BlueprintCallable, Category = "Shooter|VictoryScreen")
	void ReplayLevel();

	/** Return to main menu / level select */
	UFUNCTION(BlueprintCallable, Category = "Shooter|VictoryScreen")
	void ReturnToMainMenu();

	// ==================== Level Flow Properties ====================

	/** Name of the next level to load (set by GameMode) */
	UPROPERTY(BlueprintReadWrite, Category = "Shooter|VictoryScreen")
	FName NextLevelName;

	/** Whether this is the final level in the game */
	UPROPERTY(BlueprintReadOnly, Category = "Shooter|VictoryScreen")
	bool bIsFinalLevel = false;
};
