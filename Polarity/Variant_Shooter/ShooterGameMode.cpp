// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterGameMode.h"
#include "ShooterUI.h"
#include "ShooterCharacter.h"
#include "Polarity/Checkpoint/CheckpointSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

void AShooterGameMode::BeginPlay()
{
	Super::BeginPlay();

	// create the UI
	ShooterUI = CreateWidget<UShooterUI>(UGameplayStatics::GetPlayerController(GetWorld(), 0), ShooterUIClass);
	ShooterUI->AddToViewport(0);
}

void AShooterGameMode::IncrementTeamScore(uint8 TeamByte)
{
	// retrieve the team score if any
	int32 Score = 0;
	if (int32* FoundScore = TeamScores.Find(TeamByte))
	{
		Score = *FoundScore;
	}

	// increment the score for the given team
	++Score;
	TeamScores.Add(TeamByte, Score);

	// update the UI
	ShooterUI->BP_UpdateScore(TeamByte, Score);
}

bool AShooterGameMode::RespawnPlayerAtCheckpoint(APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		return false;
	}

	AShooterCharacter* Character = Cast<AShooterCharacter>(PlayerController->GetPawn());
	if (!Character)
	{
		return false;
	}

	UCheckpointSubsystem* CheckpointSubsystem = GetWorld()->GetSubsystem<UCheckpointSubsystem>();
	if (!CheckpointSubsystem || !CheckpointSubsystem->HasActiveCheckpoint())
	{
		// No checkpoint - restart level instead
		RestartLevel();
		return true;
	}

	return CheckpointSubsystem->RespawnAtCheckpoint(Character);
}

bool AShooterGameMode::HasCheckpointAvailable() const
{
	if (const UCheckpointSubsystem* CheckpointSubsystem = GetWorld()->GetSubsystem<UCheckpointSubsystem>())
	{
		return CheckpointSubsystem->HasActiveCheckpoint();
	}
	return false;
}

void AShooterGameMode::RestartLevel()
{
	// Clear checkpoint data first
	if (UCheckpointSubsystem* CheckpointSubsystem = GetWorld()->GetSubsystem<UCheckpointSubsystem>())
	{
		CheckpointSubsystem->ClearCheckpointData();
	}

	// Restart current level
	UGameplayStatics::OpenLevel(this, FName(*GetWorld()->GetName()));
}
