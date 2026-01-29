// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterPauseMenuUI.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

void UShooterPauseMenuUI::ResumeGame()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetPause(false);
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());

		RemoveFromParent();
	}
}

void UShooterPauseMenuUI::RestartFromCheckpoint()
{
	OnRestartFromCheckpointRequested.Broadcast();

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetPause(false);
		RemoveFromParent();
	}
}

void UShooterPauseMenuUI::OpenSettings()
{
	// Broadcast event for Blueprint to handle opening settings widget
	BP_OnOpenSettings();
}

void UShooterPauseMenuUI::QuitToMainMenu()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetPause(false);

		// TODO: Replace with actual main menu level name
		UGameplayStatics::OpenLevel(this, FName("MainMenu"));
	}
}
