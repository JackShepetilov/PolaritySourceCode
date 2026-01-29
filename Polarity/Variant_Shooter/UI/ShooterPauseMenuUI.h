// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterPauseMenuUI.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRestartFromCheckpointRequested);

/**
 * Pause Menu UI widget for the shooter game.
 * Simple menu with: Resume, Restart from Checkpoint, Settings, Quit
 * Settings menu is a separate widget.
 */
UCLASS(abstract)
class POLARITY_API UShooterPauseMenuUI : public UUserWidget
{
	GENERATED_BODY()

public:

	// ==================== Delegates ====================

	/** Broadcast when player requests to restart from last checkpoint */
	UPROPERTY(BlueprintAssignable, Category = "Shooter|PauseMenu")
	FOnRestartFromCheckpointRequested OnRestartFromCheckpointRequested;

	// ==================== Blueprint Events ====================

	/** Called when the pause menu is shown */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|PauseMenu", meta = (DisplayName = "OnMenuShown"))
	void BP_OnMenuShown();

	/** Called when the pause menu is hidden */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|PauseMenu", meta = (DisplayName = "OnMenuHidden"))
	void BP_OnMenuHidden();

	/** Called when Settings button is pressed - Blueprint should open settings widget */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|PauseMenu", meta = (DisplayName = "OnOpenSettings"))
	void BP_OnOpenSettings();

	// ==================== Menu Actions ====================

	/** Resume game - hides menu and unpauses */
	UFUNCTION(BlueprintCallable, Category = "Shooter|PauseMenu")
	void ResumeGame();

	/** Restart from last checkpoint */
	UFUNCTION(BlueprintCallable, Category = "Shooter|PauseMenu")
	void RestartFromCheckpoint();

	/** Open settings menu */
	UFUNCTION(BlueprintCallable, Category = "Shooter|PauseMenu")
	void OpenSettings();

	/** Quit to main menu */
	UFUNCTION(BlueprintCallable, Category = "Shooter|PauseMenu")
	void QuitToMainMenu();
};
