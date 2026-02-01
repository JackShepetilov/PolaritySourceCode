// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterMainMenuUI.generated.h"

class UShooterOptionsMenuUI;

/**
 * Main Menu UI widget for the shooter game.
 * Buttons: Settings, Tutorial, Level 1, Level 2, Quit Game
 * Settings menu is a separate widget (same as pause menu uses).
 */
UCLASS(abstract)
class POLARITY_API UShooterMainMenuUI : public UUserWidget
{
	GENERATED_BODY()

public:

	// ==================== Blueprint Events ====================

	/** Called when the main menu is shown */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|MainMenu", meta = (DisplayName = "OnMenuShown"))
	void BP_OnMenuShown();

	/** Called when Settings button is pressed - Blueprint should handle any additional setup */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|MainMenu", meta = (DisplayName = "OnOpenSettings"))
	void BP_OnOpenSettings();

	/** Called when returning from settings menu */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|MainMenu", meta = (DisplayName = "OnSettingsClosed"))
	void BP_OnSettingsClosed();

	/** Called when Tutorial button is pressed - Blueprint can show tutorial widget or load level */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|MainMenu", meta = (DisplayName = "OnTutorialRequested"))
	void BP_OnTutorialRequested();

	// ==================== Menu Actions ====================

	/** Open settings menu */
	UFUNCTION(BlueprintCallable, Category = "Shooter|MainMenu")
	void OpenSettings();

	/** Start tutorial */
	UFUNCTION(BlueprintCallable, Category = "Shooter|MainMenu")
	void StartTutorial();

	/** Load Level 1 */
	UFUNCTION(BlueprintCallable, Category = "Shooter|MainMenu")
	void LoadLevel1();

	/** Load Level 2 */
	UFUNCTION(BlueprintCallable, Category = "Shooter|MainMenu")
	void LoadLevel2();

	/** Quit the game */
	UFUNCTION(BlueprintCallable, Category = "Shooter|MainMenu")
	void QuitGame();

	/** Check if options menu is currently open */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shooter|MainMenu")
	bool IsOptionsMenuOpen() const;

	/** Close options menu and return to main menu */
	UFUNCTION(BlueprintCallable, Category = "Shooter|MainMenu")
	void CloseOptionsMenu();

protected:

	virtual void NativeConstruct() override;

	/** Options menu widget class to spawn */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|MainMenu|Settings")
	TSubclassOf<UShooterOptionsMenuUI> OptionsMenuWidgetClass;

	/** Spawned options menu widget */
	UPROPERTY(BlueprintReadOnly, Category = "Shooter|MainMenu|Settings")
	TObjectPtr<UShooterOptionsMenuUI> OptionsMenuWidget;

	/** Level name for Level 1 button */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|MainMenu|Levels")
	FName Level1Name = FName("Level1");

	/** Level name for Level 2 button */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|MainMenu|Levels")
	FName Level2Name = FName("Level2");

	/** Level name for Tutorial (if loading a level) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|MainMenu|Levels")
	FName TutorialLevelName = FName("Tutorial");

	/** If true, Tutorial button loads a level. If false, broadcasts BP event only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|MainMenu|Levels")
	bool bTutorialLoadsLevel = false;

private:

	/** Called when options menu closes itself */
	UFUNCTION()
	void OnOptionsMenuClosedHandler();
};
