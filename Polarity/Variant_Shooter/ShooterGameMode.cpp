// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterGameMode.h"
#include "ShooterUI.h"
#include "ShooterCharacter.h"
#include "RunSubsystem.h"
#include "RunLaunchPoint.h"
#include "Polarity/Checkpoint/CheckpointSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "TimerManager.h"

void AShooterGameMode::BeginPlay()
{
	Super::BeginPlay();

	// create the UI
	ShooterUI = CreateWidget<UShooterUI>(UGameplayStatics::GetPlayerController(GetWorld(), 0), ShooterUIClass);
	ShooterUI->AddToViewport(0);

	// ===== Run-start loading gate =====
	// At BeginPlay the level is loaded but the first frame is not yet rendered, and
	// textures/shaders may still be streaming in. We cover the viewport with a black
	// widget from frame 0, then start the run only after the world has actually drawn.

	// 1. Cover the screen immediately so the player never sees the black/unstreamed frames.
	if (LoadingCoverClass)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			LoadingCoverWidget = CreateWidget<UUserWidget>(PC, LoadingCoverClass);
			if (LoadingCoverWidget)
			{
				// Very high Z-order so it sits above the HUD and everything else.
				LoadingCoverWidget->AddToViewport(1000);
			}
		}
	}

	// 2. Wait for the first actually-rendered frame, then start the run.
	//    OnViewportRendered() is a real draw event in UE5.6; we latch the first one.
	if (GEngine && GEngine->GameViewport)
	{
		ViewportRenderedHandle = GEngine->GameViewport->OnViewportRendered().AddUObject(
			this, &AShooterGameMode::OnFirstViewportRendered);
	}
	else
	{
		// No viewport (e.g. dedicated server) — fall back to next tick.
		GetWorldTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &AShooterGameMode::HandleWorldReady));
	}
}

void AShooterGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Drop the viewport hook if we ended before the first frame was drawn.
	if (GEngine && GEngine->GameViewport && ViewportRenderedHandle.IsValid())
	{
		GEngine->GameViewport->OnViewportRendered().Remove(ViewportRenderedHandle);
	}
	ViewportRenderedHandle.Reset();

	Super::EndPlay(EndPlayReason);
}

void AShooterGameMode::OnFirstViewportRendered(FViewport* /*Viewport*/)
{
	// Unbind so we only react to the FIRST drawn frame.
	if (GEngine && GEngine->GameViewport && ViewportRenderedHandle.IsValid())
	{
		GEngine->GameViewport->OnViewportRendered().Remove(ViewportRenderedHandle);
	}
	ViewportRenderedHandle.Reset();

	// Defer out of the draw broadcast to a clean tick boundary before starting the run.
	GetWorldTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(this, &AShooterGameMode::HandleWorldReady));
}

void AShooterGameMode::HandleWorldReady()
{
	if (bRunStartTriggered)
	{
		return;
	}
	bRunStartTriggered = true;

	// TODO (when streaming sublevels are added): before this point, wait until every
	// required ULevelStreaming sublevel reports OnLevelShown. All sublevels are currently
	// Always Loaded, so they are guaranteed present here.

	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] World ready -> StartRun"));

	if (UGameInstance* GI = GetGameInstance())
	{
		if (URunSubsystem* Run = GI->GetSubsystem<URunSubsystem>())
		{
			Run->StartRun();
		}
	}

	// Toss the player out of the sea (unless BP wants to time it with the intro sequence).
	if (bAutoLaunchOnReady)
	{
		PerformRunLaunch();
	}

	// Let Blueprint play the intro Level Sequence (fade) + reveal.
	BP_OnRunStartReady();
}

void AShooterGameMode::PerformRunLaunch()
{
	ARunLaunchPoint* Point = Cast<ARunLaunchPoint>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ARunLaunchPoint::StaticClass()));
	if (!Point)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RUN_DEBUG] PerformRunLaunch: no ARunLaunchPoint in level"));
		return;
	}

	AShooterCharacter* Character = Cast<AShooterCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
	if (!Character)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RUN_DEBUG] PerformRunLaunch: player pawn is not a ShooterCharacter"));
		return;
	}

	Character->SetActorLocationAndRotation(Point->GetActorLocation(), Point->GetActorRotation(),
		false, nullptr, ETeleportType::TeleportPhysics);
	Character->BeginRunLaunch(Point->GetLaunchVelocity());
}

void AShooterGameMode::DismissLoadingCover()
{
	if (LoadingCoverWidget)
	{
		LoadingCoverWidget->RemoveFromParent();
		LoadingCoverWidget = nullptr;
	}
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
