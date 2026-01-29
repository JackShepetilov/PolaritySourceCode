// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterDeathScreenUI.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

void UShooterDeathScreenUI::Respawn()
{
	// Broadcast delegate for GameMode to handle respawn logic
	OnRespawnRequested.Broadcast();

	// Remove the death screen
	RemoveFromParent();
}

void UShooterDeathScreenUI::RespawnAtStart()
{
	// Broadcast delegate for GameMode to handle respawn at start
	OnRespawnAtStartRequested.Broadcast();

	// Remove the death screen
	RemoveFromParent();
}

void UShooterDeathScreenUI::RestartLevel()
{
	// Get current level name and reload it
	FString CurrentLevel = GetWorld()->GetMapName();
	CurrentLevel.RemoveFromStart(GetWorld()->StreamingLevelsPrefix);

	UGameplayStatics::OpenLevel(this, FName(*CurrentLevel));
}

void UShooterDeathScreenUI::ReturnToMainMenu()
{
	// TODO: Replace with actual main menu level name
	UGameplayStatics::OpenLevel(this, FName("MainMenu"));
}
