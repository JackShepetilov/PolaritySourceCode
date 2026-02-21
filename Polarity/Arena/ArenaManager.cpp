// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "ArenaManager.h"
#include "ArenaSpawnPoint.h"
#include "Polarity/Variant_Shooter/ShooterCharacter.h"
#include "Polarity/Variant_Shooter/AI/ShooterNPC.h"
#include "Polarity/Variant_Shooter/AI/FlyingDrone.h"
#include "Polarity/Variant_Shooter/AI/ShooterAIController.h"
#include "Polarity/Checkpoint/CheckpointSubsystem.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Components/StateTreeAIComponent.h"
#include "Components/PrimitiveComponent.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "Polarity/Variant_Shooter/ShooterDoor.h"

AArenaManager::AArenaManager()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

// ==================== Lifecycle ====================

void AArenaManager::BeginPlay()
{
	Super::BeginPlay();

	CheckpointSubsystem = GetWorld()->GetSubsystem<UCheckpointSubsystem>();

	// Register overlap callbacks FIRST (before changing collision)
	RegisterBlockerOverlaps();

	// Start with blockers invisible and passable, but overlap-capable for trigger detection
	SetBlockersActive(false);

	// Bind to player respawn so we can reset
	if (CheckpointSubsystem && !bBoundToRespawn)
	{
		CheckpointSubsystem->OnPlayerRespawned.AddDynamic(this, &AArenaManager::OnPlayerRespawned);
		bBoundToRespawn = true;
	}
}

void AArenaManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(WaveTimerHandle);
	GetWorldTimerManager().ClearTimer(ActivationDelayHandle);

	if (CheckpointSubsystem && bBoundToRespawn)
	{
		CheckpointSubsystem->OnPlayerRespawned.RemoveDynamic(this, &AArenaManager::OnPlayerRespawned);
		bBoundToRespawn = false;
	}

	Super::EndPlay(EndPlayReason);
}

// ==================== Activation ====================

void AArenaManager::RegisterBlockerOverlaps()
{
	for (const TSoftObjectPtr<AActor>& BlockerRef : ExitBlockers)
	{
		AActor* Blocker = BlockerRef.Get();
		if (!Blocker)
		{
			continue;
		}

		// Find the first primitive component on the blocker to use as overlap trigger
		TArray<UPrimitiveComponent*> Primitives;
		Blocker->GetComponents<UPrimitiveComponent>(Primitives);

		for (UPrimitiveComponent* Prim : Primitives)
		{
			Prim->SetGenerateOverlapEvents(true);
			Prim->OnComponentBeginOverlap.AddDynamic(this, &AArenaManager::OnBlockerBeginOverlap);
			break; // Only bind first primitive per blocker
		}
	}
}

void AArenaManager::OnBlockerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (CurrentState != EArenaState::Idle)
	{
		return;
	}

	AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor);
	if (!Player)
	{
		return;
	}

	// Player touched the blocker boundary — remember them and wait
	// so they have time to fully pass through the invisible shell
	PendingPlayer = Player;

	GetWorldTimerManager().SetTimer(
		ActivationDelayHandle,
		this,
		&AArenaManager::OnActivationDelayFinished,
		0.4f,
		false
	);
}

void AArenaManager::OnActivationDelayFinished()
{
	if (CurrentState != EArenaState::Idle)
	{
		return;
	}

	AShooterCharacter* Player = PendingPlayer.Get();
	if (!Player)
	{
		return;
	}

	PendingPlayer.Reset();

	// Check that the player is inside the blocker, not outside.
	// Blocker is a hemisphere — check distance from player to blocker center.
	// Blocker's default diameter is 100 units (radius 50), scaled by actor scale.
	bool bPlayerInside = false;
	for (const TSoftObjectPtr<AActor>& BlockerRef : ExitBlockers)
	{
		AActor* Blocker = BlockerRef.Get();
		if (!Blocker)
		{
			continue;
		}

		const FVector BlockerCenter = Blocker->GetActorLocation();
		const FVector PlayerLocation = Player->GetActorLocation();
		const float Distance = FVector::Dist2D(PlayerLocation, BlockerCenter);

		// Radius = 80 (half of 160-unit / 1.6m diameter) * actor's uniform scale
		const float BlockerRadius = 80.0f * Blocker->GetActorScale3D().X;

		UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Dist2D=%.1f, BlockerRadius=%.1f, Scale=%.2f"), Distance, BlockerRadius, Blocker->GetActorScale3D().X);

		if (Distance < BlockerRadius)
		{
			bPlayerInside = true;
			break;
		}
	}

	if (!bPlayerInside)
	{
		UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Player outside blocker — activation cancelled"));
		return;
	}

	ActivateArena(Player);
}

void AArenaManager::ActivateArena(AShooterCharacter* Player)
{
	if (Waves.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ArenaManager: No waves configured, skipping activation"));
		return;
	}

	CurrentState = EArenaState::Active;

	// Close exits
	SetBlockersActive(true);

	// Save checkpoint so player respawns here on death
	SaveArenaCheckpoint(Player);

	OnArenaStarted.Broadcast();

	// Start first wave
	SpawnWave(0);
}

// ==================== Blockers ====================

void AArenaManager::SetBlockersActive(bool bActive)
{
	for (const TSoftObjectPtr<AActor>& BlockerRef : ExitBlockers)
	{
		AActor* Blocker = BlockerRef.Get();
		if (!Blocker)
		{
			continue;
		}

		// Always keep actor collision enabled (needed for overlap trigger detection)
		// Instead, toggle collision RESPONSE on each primitive:
		//   Active:   visible + blocks movement (BlockAll)
		//   Inactive: invisible + overlap only (OverlapAll) for trigger detection
		Blocker->SetActorHiddenInGame(!bActive);

		TArray<UPrimitiveComponent*> Primitives;
		Blocker->GetComponents<UPrimitiveComponent>(Primitives);

		for (UPrimitiveComponent* Prim : Primitives)
		{
			if (bActive)
			{
				Prim->SetCollisionResponseToAllChannels(ECR_Block);
				Prim->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
			else
			{
				Prim->SetCollisionResponseToAllChannels(ECR_Overlap);
				Prim->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			}
		}
	}
}

// ==================== Wave Spawning ====================

void AArenaManager::SpawnWave(int32 WaveIndex)
{
	if (!Waves.IsValidIndex(WaveIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("ArenaManager: Invalid wave index %d"), WaveIndex);
		return;
	}

	CurrentWaveIndex = WaveIndex;
	CurrentState = EArenaState::Active;
	AliveNPCs.Empty();

	const FArenaWave& Wave = Waves[WaveIndex];

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Track which spawn points are used this wave to avoid stacking NPCs
	TArray<AArenaSpawnPoint*> UsedSpawnPoints;

	for (const FArenaSpawnEntry& Entry : Wave.Entries)
	{
		if (!Entry.NPCClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Null NPC class in wave %d"), WaveIndex);
			continue;
		}

		// Check if this is a flying drone for air spawn point selection
		const bool bIsFlyingUnit = Entry.NPCClass->IsChildOf(AFlyingDrone::StaticClass());

		for (int32 i = 0; i < Entry.Count; ++i)
		{
			AArenaSpawnPoint* SpawnPoint = PickSpawnPoint(Entry.NPCClass, UsedSpawnPoints);
			if (!SpawnPoint)
			{
				UE_LOG(LogTemp, Warning, TEXT("ArenaManager: No valid spawn point for %s"), *Entry.NPCClass->GetName());
				continue;
			}

			UsedSpawnPoints.Add(SpawnPoint);

			const FTransform SpawnTransform = SpawnPoint->GetSpawnTransform(bIsFlyingUnit);

			APawn* SpawnedPawn = UAIBlueprintHelperLibrary::SpawnAIFromClass(
				World,
				Entry.NPCClass,
				nullptr, // No BehaviorTree — StateTree configured on controller
				SpawnTransform.GetLocation(),
				SpawnTransform.Rotator(),
				true // bNoCollisionFail
			);

			if (AShooterNPC* NPC = Cast<AShooterNPC>(SpawnedPawn))
			{
				AliveNPCs.Add(NPC);

				// Subscribe to death
				NPC->OnNPCDeath.AddDynamic(this, &AArenaManager::OnNPCDied);
			}
		}
	}

	OnWaveStarted.Broadcast(WaveIndex);

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Wave %d started — %d NPCs spawned"), WaveIndex, AliveNPCs.Num());

	// Force all NPCs to target the player immediately — don't rely on perception senses
	// which may fail if player is behind the NPC or out of sight angle.
	// SetCurrentTarget directly assigns the enemy, then ForcePerceptionUpdate on next tick
	// ensures the perception system catches up.
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	AActor* PlayerActor = PC ? PC->GetPawn() : nullptr;

	if (PlayerActor)
	{
		for (const TWeakObjectPtr<AShooterNPC>& NPCPtr : AliveNPCs)
		{
			if (AShooterNPC* NPC = NPCPtr.Get())
			{
				if (AShooterAIController* AIController = Cast<AShooterAIController>(NPC->GetController()))
				{
					AIController->SetCurrentTarget(PlayerActor);
				}
			}
		}
	}

	// Also refresh perception on next tick so the system stays in sync
	GetWorldTimerManager().SetTimerForNextTick([this]()
	{
		for (const TWeakObjectPtr<AShooterNPC>& NPCPtr : AliveNPCs)
		{
			if (AShooterNPC* NPC = NPCPtr.Get())
			{
				if (AShooterAIController* AIController = Cast<AShooterAIController>(NPC->GetController()))
				{
					AIController->ForcePerceptionUpdate();
				}
			}
		}
	});
}

AArenaSpawnPoint* AArenaManager::PickSpawnPoint(TSubclassOf<AShooterNPC> NPCClass, const TArray<AArenaSpawnPoint*>& UsedPoints) const
{
	const bool bNeedsAirSpawn = NPCClass->IsChildOf(AFlyingDrone::StaticClass());

	// Collect valid spawn points, excluding already used ones
	TArray<AArenaSpawnPoint*> ValidPoints;
	for (const TSoftObjectPtr<AArenaSpawnPoint>& PointRef : SpawnPoints)
	{
		AArenaSpawnPoint* Point = PointRef.Get();
		if (!Point || UsedPoints.Contains(Point))
		{
			continue;
		}

		if (bNeedsAirSpawn && Point->bAirSpawn)
		{
			ValidPoints.Add(Point);
		}
		else if (!bNeedsAirSpawn && !Point->bAirSpawn)
		{
			ValidPoints.Add(Point);
		}
	}

	// Fallback 1: if no unused matching type, try any unused point
	if (ValidPoints.Num() == 0)
	{
		for (const TSoftObjectPtr<AArenaSpawnPoint>& PointRef : SpawnPoints)
		{
			AArenaSpawnPoint* Point = PointRef.Get();
			if (Point && !UsedPoints.Contains(Point))
			{
				ValidPoints.Add(Point);
			}
		}
	}

	// Fallback 2: if all points used, allow reuse (more NPCs than spawn points)
	if (ValidPoints.Num() == 0)
	{
		for (const TSoftObjectPtr<AArenaSpawnPoint>& PointRef : SpawnPoints)
		{
			if (AArenaSpawnPoint* Point = PointRef.Get())
			{
				ValidPoints.Add(Point);
			}
		}
	}

	if (ValidPoints.Num() == 0)
	{
		return nullptr;
	}

	return ValidPoints[FMath::RandRange(0, ValidPoints.Num() - 1)];
}

// ==================== NPC Death Tracking ====================

void AArenaManager::OnNPCDied(AShooterNPC* DeadNPC)
{
	if (!DeadNPC)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager::OnNPCDied — %s died. AliveNPCs before: %d"), *DeadNPC->GetName(), AliveNPCs.Num());

	// Remove from alive list
	AliveNPCs.RemoveAll([DeadNPC](const TWeakObjectPtr<AShooterNPC>& Ptr)
	{
		return !Ptr.IsValid() || Ptr.Get() == DeadNPC;
	});

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager::OnNPCDied — AliveNPCs after: %d, CurrentState: %d"), AliveNPCs.Num(), (int32)CurrentState);

	CheckWaveComplete();
}

void AArenaManager::CheckWaveComplete()
{
	// Clean up any stale weak pointers
	AliveNPCs.RemoveAll([](const TWeakObjectPtr<AShooterNPC>& Ptr)
	{
		return !Ptr.IsValid();
	});

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager::CheckWaveComplete — AliveNPCs: %d, CurrentState: %d"), AliveNPCs.Num(), (int32)CurrentState);

	if (AliveNPCs.Num() > 0)
	{
		return;
	}

	// Wave cleared
	UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Wave %d cleared! Advancing..."), CurrentWaveIndex);
	OnWaveCleared.Broadcast(CurrentWaveIndex);

	const int32 NextWaveIndex = CurrentWaveIndex + 1;

	if (Waves.IsValidIndex(NextWaveIndex))
	{
		// More waves — start timer for next wave
		CurrentState = EArenaState::BetweenWaves;

		float Delay = TimeBetweenWaves + Waves[NextWaveIndex].DelayBeforeWave;

		UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Next wave %d in %.1f seconds"), NextWaveIndex, Delay);

		GetWorldTimerManager().SetTimer(
			WaveTimerHandle,
			this,
			&AArenaManager::StartNextWave,
			Delay,
			false
		);
	}
	else
	{
		// All waves done
		UE_LOG(LogTemp, Warning, TEXT("ArenaManager: All waves done, completing arena"));
		CompleteArena();
	}
}

void AArenaManager::StartNextWave()
{
	const int32 NextWaveIndex = CurrentWaveIndex + 1;

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager::StartNextWave — NextWaveIndex: %d, Valid: %d"), NextWaveIndex, Waves.IsValidIndex(NextWaveIndex));

	if (Waves.IsValidIndex(NextWaveIndex))
	{
		SpawnWave(NextWaveIndex);
	}
}

// ==================== Completion ====================

void AArenaManager::CompleteArena()
{
	CurrentState = EArenaState::Completed;

	// Open exits
	SetBlockersActive(false);

	OnArenaCleared.Broadcast();

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Arena completed!"));
}

// ==================== Reset ====================

void AArenaManager::ResetArena()
{
	// Cancel wave timer
	GetWorldTimerManager().ClearTimer(WaveTimerHandle);

	// Destroy all alive NPCs with proper cleanup
	for (const TWeakObjectPtr<AShooterNPC>& NPCPtr : AliveNPCs)
	{
		if (AShooterNPC* NPC = NPCPtr.Get())
		{
			// Clean up controller/StateTree before destroying
			if (AShooterAIController* AIController = Cast<AShooterAIController>(NPC->GetController()))
			{
				if (UStateTreeAIComponent* StateTreeComp = AIController->FindComponentByClass<UStateTreeAIComponent>())
				{
					StateTreeComp->StopLogic(TEXT("ArenaReset"));
				}
				AIController->UnPossess();
				AIController->Destroy();
			}
			NPC->Destroy();
		}
	}
	AliveNPCs.Empty();

	// Hide blockers (passage open)
	SetBlockersActive(false);

	// Reset state
	CurrentState = EArenaState::Idle;
	CurrentWaveIndex = -1;

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Arena reset to Idle"));
}

void AArenaManager::OnPlayerRespawned()
{
	UE_LOG(LogTemp, Warning, TEXT("ArenaManager::OnPlayerRespawned — CurrentState: %d"), (int32)CurrentState);

	// Only reset if we were in an active fight
	if (CurrentState == EArenaState::Active || CurrentState == EArenaState::BetweenWaves)
	{
		ResetArena();

		// Player respawns inside the arena (at PlayerRespawnPoint),
		// so BeginOverlap won't fire again. Re-activate immediately.
		AShooterCharacter* Player = Cast<AShooterCharacter>(GetWorld()->GetFirstPlayerController()->GetPawn());
		if (Player)
		{
			UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Re-activating arena after respawn"));
			ActivateArena(Player);
		}
	}
}

// ==================== Checkpoint ====================

void AArenaManager::SaveArenaCheckpoint(AShooterCharacter* Player)
{
	if (!CheckpointSubsystem || !Player)
	{
		return;
	}

	FCheckpointData NewData;
	NewData.bIsValid = true;
	NewData.CheckpointID = FGuid::NewGuid();

	// Use arena's dedicated respawn point, or fall back to player position
	if (AActor* RespawnActor = PlayerRespawnPoint.Get())
	{
		NewData.SpawnTransform = RespawnActor->GetActorTransform();
	}
	else
	{
		NewData.SpawnTransform = Player->GetActorTransform();
		UE_LOG(LogTemp, Warning, TEXT("ArenaManager: No PlayerRespawnPoint set, using player's current position"));
	}

	// Save player state (health, charge, weapons)
	Player->SaveToCheckpoint(NewData);

	// Set directly on the subsystem (arena NPCs are managed by ArenaManager, not checkpoint system)
	CheckpointSubsystem->SetCheckpointData(NewData);

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Checkpoint saved at arena respawn point"));
}
