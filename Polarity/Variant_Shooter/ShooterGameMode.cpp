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
#include "Engine/LevelStreaming.h"
#include "ContentStreaming.h"
#include "TimerManager.h"

namespace
{
	/** True while any streaming sublevel is still loading or not yet visible. */
	bool AnySublevelStillStreaming(const UWorld* World)
	{
		for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
		{
			if (StreamingLevel && (!StreamingLevel->IsLevelLoaded() || !StreamingLevel->IsLevelVisible()))
			{
				return true;
			}
		}
		return false;
	}
}

void AShooterGameMode::BeginPlay()
{
	Super::BeginPlay();

	// create the UI
	ShooterUI = CreateWidget<UShooterUI>(UGameplayStatics::GetPlayerController(GetWorld(), 0), ShooterUIClass);
	ShooterUI->AddToViewport(0);

	// ===== Run-start loading gate =====
	// Run maps are identified by a RunLaunchPoint marker. On non-run maps (menu/hub) there is none,
	// so the gate stays completely idle: no cover, no run-start sequence. The marker may live in a
	// streaming sublevel — the lookup retries while sublevels are still being added (standalone).
	TryInitRunGate();
}

void AShooterGameMode::EnsureLoadingCover()
{
	// Cover the screen so the player never sees the black/unstreamed frames.
	if (LoadingCoverWidget || !LoadingCoverClass)
	{
		return;
	}
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

void AShooterGameMode::TryInitRunGate()
{
	if (bRunStartTriggered)
	{
		return;
	}

	RunMarker = Cast<ARunLaunchPoint>(UGameplayStatics::GetActorOfClass(GetWorld(), ARunLaunchPoint::StaticClass()));
	if (!RunMarker)
	{
		// In PIE the duplicated editor world already contains every always-loaded sublevel at
		// BeginPlay, but in standalone/packaged AddToWorld is time-sliced and sublevel actors
		// appear a few frames later. While anything is still streaming in, keep the screen
		// covered and retry; only a complete world with no marker means "not a run map".
		if (AnySublevelStillStreaming(GetWorld()))
		{
			EnsureLoadingCover();
			GetWorldTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(this, &AShooterGameMode::TryInitRunGate));
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] No RunLaunchPoint after all sublevels loaded - non-run map, gate idle"));
		DismissLoadingCover();
		return;
	}

	// At BeginPlay the level is loaded but the first frame is not yet rendered, and textures/shaders
	// may still be streaming in. We cover the viewport from frame 0, then fire the run-start sequence
	// only after the world has actually drawn.

	// 1. Cover the screen immediately so the player never sees the black/unstreamed frames.
	EnsureLoadingCover();

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
	if (bRunStartTriggered || !RunMarker)
	{
		return;
	}

	// Every always-loaded sublevel must be fully added before the BP run sequence touches
	// their actors (EnterArena etc.) — in standalone AddToWorld is time-sliced and arenas
	// can lag the persistent level by several frames.
	if (AnySublevelStillStreaming(GetWorld()))
	{
		GetWorldTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &AShooterGameMode::HandleWorldReady));
		return;
	}
	bRunStartTriggered = true;

	// The first rendered frame is NOT "the black screen is gone": in standalone/packaged the engine
	// keeps showing black while textures/meshes stream in, yet the viewport already draws (black) and
	// fires OnViewportRendered. So force all wanting resources to stream in NOW and block until done
	// (bounded, so we never hard-hang). We're behind the black cover, so this hitch is invisible — and
	// it guarantees the toss plays on an actually-visible scene instead of behind the loading black.
	IStreamingManager::Get().StreamAllResources(/*TimeLimit*/ 5.0f);

	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] World ready (resources streamed) -> BP_OnRunStartReady (arena=%d, toss=%d, boss=%d)"),
		RunMarker->ArenaIndex, RunMarker->bLaunchFromSea ? 1 : 0, RunMarker->bBossIntro ? 1 : 0);

	// Blueprint owns the run-subsystem sequence (stream overlay, configs, StartRun, EnterArena) and the
	// intro flow. It calls PerformRunLaunch() for the toss, or plays the boss cutscene + ForceActivateArena
	// for the boss branch, picking by bLaunchFromSea / bBossIntro.
	BP_OnRunStartReady(RunMarker->ArenaIndex, RunMarker->bLaunchFromSea, RunMarker->bBossIntro);
}

void AShooterGameMode::PerformRunLaunch()
{
	if (!RunMarker)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RUN_DEBUG] PerformRunLaunch: no RunLaunchPoint marker"));
		return;
	}

	AShooterCharacter* Character = Cast<AShooterCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
	if (!Character)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RUN_DEBUG] PerformRunLaunch: player pawn is not a ShooterCharacter"));
		return;
	}

	// Keep the capsule upright: take ONLY the marker's yaw for facing. The marker's pitch/roll is
	// meant to aim the toss arc and lives in the launch VELOCITY (GetLaunchVelocity uses the marker's
	// forward vector) — it must never tilt the character's body, or the mesh/camera break.
	const FRotator UprightRot(0.f, RunMarker->GetActorRotation().Yaw, 0.f);
	Character->SetActorLocationAndRotation(RunMarker->GetActorLocation(), UprightRot,
		false, nullptr, ETeleportType::TeleportPhysics);
	Character->BeginRunLaunch(RunMarker->GetLaunchVelocity());
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
