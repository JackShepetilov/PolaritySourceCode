// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterVictoryScreenUI.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

void UShooterVictoryScreenUI::ContinueToNextLevel()
{
	if (bIsFinalLevel || NextLevelName.IsNone())
	{
		// No next level - return to main menu
		ReturnToMainMenu();
		return;
	}

	// Load the next level
	UGameplayStatics::OpenLevel(this, NextLevelName);
}

void UShooterVictoryScreenUI::ReplayLevel()
{
	// Get current level name and reload it
	FString CurrentLevel = GetWorld()->GetMapName();
	CurrentLevel.RemoveFromStart(GetWorld()->StreamingLevelsPrefix);

	UGameplayStatics::OpenLevel(this, FName(*CurrentLevel));
}

void UShooterVictoryScreenUI::ReturnToMainMenu()
{
	// TODO: Replace with actual main menu level name
	UGameplayStatics::OpenLevel(this, FName("MainMenu"));
}
