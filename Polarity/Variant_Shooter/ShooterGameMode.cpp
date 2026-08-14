// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterGameMode.h"
#include "Coop/CoopPlayers.h"
#include "Components/CapsuleComponent.h"
#include "ShooterUI.h"
#include "ShooterCharacter.h"
#include "RunSubsystem.h"
#include "RunLaunchPoint.h"
#include "Run/Generation/BiomeRunAssembler.h"
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

	// The HUD is not built here any more. A widget made by the GameMode lives on the server and
	// belongs to player zero, so in coop the host got a HUD and every client got nothing. Each
	// character now builds its own on its own machine, asking this GameMode only for the class
	// (AShooterCharacter::HUDClass). Score updates reach them through IncrementTeamScore below.

	// ===== Run-start loading gate =====
	// Run maps are identified by a RunLaunchPoint marker. On non-run maps (menu/hub) there is none,
	// so the gate stays completely idle: no cover, no run-start sequence. The marker may live in a
	// streaming sublevel — the lookup retries while sublevels are still being added (standalone).
	TryInitRunGate();
}

TSubclassOf<UUserWidget> AShooterGameMode::ResolveLoadingCoverClass() const
{
	TSubclassOf<UUserWidget> CoverClass = LoadingCoverClass;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (URunSubsystem* Run = GI->GetSubsystem<URunSubsystem>())
		{
			if (TSubclassOf<UUserWidget> RequestedClass = Run->GetRunLoadingScreenClass())
			{
				CoverClass = RequestedClass;
			}
		}
	}
	return CoverClass;
}

void AShooterGameMode::EnsureLoadingCover()
{
	// Cover every player's screen, not player zero's. A run starts for the whole team, and a client
	// left uncovered watches the unstreamed frames the cover exists to hide.
	const TSubclassOf<UUserWidget> CoverClass = ResolveLoadingCoverClass();
	if (!CoverClass)
	{
		return;
	}

	TArray<APawn*> Players;
	CoopPlayers::GetAll(GetWorld(), Players);
	for (APawn* Player : Players)
	{
		if (AShooterCharacter* Character = Cast<AShooterCharacter>(Player))
		{
			Character->Client_ShowLoadingCover(CoverClass);
		}
	}
}

void AShooterGameMode::TryInitRunGate()
{
	if (bRunStartTriggered)
	{
		return;
	}

	BiomeRunAssembler = Cast<ABiomeRunAssembler>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ABiomeRunAssembler::StaticClass()));
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

	// Semi-procedural maps add their selected arena sublevels during BeginPlay. Wait until those
	// levels, Landscape Grass exclusions and the one-time nav build are all ready as well.
	if (BiomeRunAssembler)
	{
		if (BiomeRunAssembler->HasAssemblyFailed())
		{
			UE_LOG(LogTemp, Error, TEXT("[RUN_DEBUG] Biome assembly failed; keeping loading cover visible"));
			return;
		}
		if (!BiomeRunAssembler->IsAssemblyReady())
		{
			GetWorldTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(this, &AShooterGameMode::HandleWorldReady));
			return;
		}
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

	// The whole team gets tossed into the run, not just the host.
	TArray<APawn*> PlayerPawns;
	CoopPlayers::GetAll(GetWorld(), PlayerPawns);

	// Keep the capsule upright: take ONLY the marker's yaw for facing. The marker's pitch/roll is
	// meant to aim the toss arc and lives in the launch VELOCITY (GetLaunchVelocity uses the marker's
	// forward vector) — it must never tilt the character's body, or the mesh/camera break.
	const FRotator UprightRot(0.f, RunMarker->GetActorRotation().Yaw, 0.f);

	// Everyone gets the SAME velocity, so the team flies as one group and lands in the same spread
	// it started in. Only the starting point is scattered, in the horizontal plane, far enough apart
	// that the capsules never begin overlapped: evenly spaced headings keep them separated, the
	// jitter on heading and radius keeps the formation from looking laid out.
	const FVector LaunchVelocity = RunMarker->GetLaunchVelocity();
	const FVector LaunchOrigin = RunMarker->GetActorLocation();
	const int32 PlayerCount = PlayerPawns.Num();

	int32 LaunchedCount = 0;
	for (int32 Index = 0; Index < PlayerCount; ++Index)
	{
		AShooterCharacter* Character = Cast<AShooterCharacter>(PlayerPawns[Index]);
		if (!Character)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RUN_DEBUG] PerformRunLaunch: player pawn is not a ShooterCharacter"));
			continue;
		}

		FVector SpawnLocation = LaunchOrigin;

		// A single player launches from the marker itself, exactly as before.
		if (PlayerCount > 1)
		{
			const float CapsuleRadius = Character->GetCapsuleComponent()
				? Character->GetCapsuleComponent()->GetScaledCapsuleRadius()
				: 34.0f;

			// Radius is measured in capsule widths, so the spread scales with the character instead
			// of being a magic distance: min keeps two capsules clear of each other, max keeps the
			// group tight enough to still read as one toss.
			const float MinRadius = CapsuleRadius * 2.5f;
			const float MaxRadius = CapsuleRadius * 4.0f;

			const float SliceAngle = 2.0f * PI / static_cast<float>(PlayerCount);
			const float Heading = SliceAngle * Index + FMath::FRandRange(-SliceAngle * 0.3f, SliceAngle * 0.3f);
			const float Radius = FMath::FRandRange(MinRadius, MaxRadius);

			SpawnLocation += FVector(FMath::Cos(Heading) * Radius, FMath::Sin(Heading) * Radius, 0.0f);
		}

		Character->SetActorLocationAndRotation(SpawnLocation, UprightRot,
			false, nullptr, ETeleportType::TeleportPhysics);
		Character->BeginRunLaunch(LaunchVelocity);
		LaunchedCount++;
	}

	if (LaunchedCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RUN_DEBUG] PerformRunLaunch: nobody to launch"));
		return;
	}

	GetWorldTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(this, &AShooterGameMode::DismissRunTransitionScreenAfterLaunch));
}

void AShooterGameMode::DismissRunTransitionScreenAfterLaunch()
{
	DismissLoadingCover();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (URunSubsystem* Run = GI->GetSubsystem<URunSubsystem>())
		{
			Run->DismissRunLoadingScreen();
		}
	}
}

void AShooterGameMode::DismissLoadingCover()
{
	TArray<APawn*> Players;
	CoopPlayers::GetAll(GetWorld(), Players);
	for (APawn* Player : Players)
	{
		if (AShooterCharacter* Character = Cast<AShooterCharacter>(Player))
		{
			Character->Client_DismissLoadingCover();
		}
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

	// update the UI on every machine that has one — the score belongs to the team, not to the host.
	TArray<APawn*> Players;
	CoopPlayers::GetAll(GetWorld(), Players);
	for (APawn* Player : Players)
	{
		if (AShooterCharacter* Character = Cast<AShooterCharacter>(Player))
		{
			Character->Client_UpdateScore(TeamByte, Score);
		}
	}
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
