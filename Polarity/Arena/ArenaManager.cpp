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
#include "Components/CapsuleComponent.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "NavigationSystem.h"
#include "Polarity/Variant_Shooter/ShooterDoor.h"
#include "DestructibleIslandActor.h"
#include "Polarity/EMFPhysicsProp.h"
#include "Polarity/Variant_Shooter/Weapons/EMFProjectile.h"
#include "Kismet/GameplayStatics.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Field/FieldSystemObjects.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/OverlapResult.h"
#include "Engine/Brush.h"
#include "GameFramework/Volume.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "RewardContainer.h"
#include "Polarity/Variant_Shooter/ShooterDummy.h"

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

	// Bind to tracked EMF props for critical velocity events
	RegisterTrackedProps();

	// Bind to linked destructible island
	if (ADestructibleIslandActor* Island = LinkedIsland.Get())
	{
		Island->OnIslandDestroyed.AddDynamic(this, &AArenaManager::OnLinkedIslandDestroyed);
	}

	// Bind to reward dummy death
	if (AShooterDummy* Dummy = RewardDummy.Get())
	{
		Dummy->OnDummyDeath.AddDynamic(this, &AArenaManager::OnRewardDummyDeath);
	}
}

void AArenaManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(WaveTimerHandle);
	GetWorldTimerManager().ClearTimer(ActivationDelayHandle);

	for (FTimerHandle& Handle : DestructionTimerHandles)
	{
		GetWorldTimerManager().ClearTimer(Handle);
	}
	DestructionTimerHandles.Empty();

	// Restore camera control if locked
	if (bCameraLocked)
	{
		EndCameraLock();
	}

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
			FVector SpawnLocation = SpawnTransform.GetLocation();

			// For ground units: project spawn location to floor + capsule half-height
			// to prevent spawning with feet in the ground
			if (!bIsFlyingUnit)
			{
				// Get capsule half-height from CDO to know how high to place the NPC
				// Add 10cm padding to ensure feet never clip into the floor
				const ACharacter* CDO = Entry.NPCClass->GetDefaultObject<ACharacter>();
				const float CapsuleHalfHeight = (CDO ? CDO->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 96.0f) + 10.0f;

				// Try NavMesh projection first — guarantees a walkable surface
				UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				if (NavSys)
				{
					FNavLocation NavResult;
					const FVector ProjectionExtent(50.0f, 50.0f, 500.0f);
					if (NavSys->ProjectPointToNavigation(SpawnLocation, NavResult, ProjectionExtent))
					{
						// NavMesh gives us the floor Z — place capsule center above it
						SpawnLocation.X = NavResult.Location.X;
						SpawnLocation.Y = NavResult.Location.Y;
						SpawnLocation.Z = NavResult.Location.Z + CapsuleHalfHeight;
					}
					else
					{
						// NavMesh projection failed — fall back to ground trace
						FHitResult GroundHit;
						FCollisionQueryParams TraceParams;
						const FVector TraceStart = SpawnLocation + FVector(0.0f, 0.0f, 200.0f);
						const FVector TraceEnd = SpawnLocation - FVector(0.0f, 0.0f, 500.0f);

						if (World->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams))
						{
							SpawnLocation.Z = GroundHit.ImpactPoint.Z + CapsuleHalfHeight;
						}
					}
				}
			}

			APawn* SpawnedPawn = UAIBlueprintHelperLibrary::SpawnAIFromClass(
				World,
				Entry.NPCClass,
				nullptr, // No BehaviorTree — StateTree configured on controller
				SpawnLocation,
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

	// Restore camera if locked
	if (bCameraLocked)
	{
		EndCameraLock();
	}

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

// ==================== Tracked Props ====================

void AArenaManager::RegisterTrackedProps()
{
	// Register explicitly tracked props
	TSet<AEMFPhysicsProp*> AlreadyRegistered;
	for (const TSoftObjectPtr<AEMFPhysicsProp>& PropRef : TrackedProps)
	{
		if (AEMFPhysicsProp* Prop = PropRef.Get())
		{
			Prop->OnCriticalVelocityImpact.AddDynamic(this, &AArenaManager::OnTrackedPropCriticalImpact);
			AlreadyRegistered.Add(Prop);
		}
	}

	// Also register ALL props in the level (for props thrown into arena dynamically)
	TArray<AActor*> AllProps;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AEMFPhysicsProp::StaticClass(), AllProps);
	for (AActor* PropActor : AllProps)
	{
		AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(PropActor);
		if (Prop && !AlreadyRegistered.Contains(Prop))
		{
			Prop->OnCriticalVelocityImpact.AddDynamic(this, &AArenaManager::OnTrackedPropCriticalImpact);
		}
	}
}

void AArenaManager::OnTrackedPropCriticalImpact(AEMFPhysicsProp* Prop, FVector Location, float Speed)
{
	NotifyCriticalImpact(Prop, Location, Speed);
}

void AArenaManager::OnProjectileCriticalImpact(AEMFProjectile* Projectile, FVector Location, float Speed)
{
	NotifyCriticalImpact(Projectile, Location, Speed);
}

void AArenaManager::NotifyCriticalImpact(AActor* Source, FVector Location, float Speed)
{
	// Only react if the impact is within our arena's destruction radius
	if (FVector::Dist(Location, GetActorLocation()) > DestructionRadius)
	{
		return;
	}

	// Check minimum distance — player must be far enough from arena center
	if (MinDestructionDistance > 0.0f)
	{
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				const float PlayerDist = FVector::Dist(Pawn->GetActorLocation(), GetActorLocation());
				if (PlayerDist < MinDestructionDistance)
				{
					UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Player too close (%.0f < %.0f), ignoring critical impact"),
						PlayerDist, MinDestructionDistance);
					return;
				}
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Critical velocity impact from %s at speed %.0f cm/s"),
		Source ? *Source->GetName() : TEXT("NULL"), Speed);

	OnCriticalVelocityImpact.Broadcast(Source, Location, Speed);

	ExecuteArenaDestruction(Location);

	// Lock camera on epicenter for cinematic effect
	StartCameraLock(Location);
}

// ==================== Arena Destruction ====================

void AArenaManager::ExecuteArenaDestruction(const FVector& Epicenter)
{
	if (bDestructionExecuted || !DestructionGC || !GetWorld())
	{
		return;
	}
	bDestructionExecuted = true;

	// Build exclusion set for fast lookup
	TSet<AActor*> ExcludedActors;
	ExcludedActors.Add(this);
	for (int32 i = 0; i < DestructionExcluded.Num(); i++)
	{
		const TSoftObjectPtr<AActor>& Ref = DestructionExcluded[i];
		if (AActor* Actor = Ref.LoadSynchronous())
		{
			ExcludedActors.Add(Actor);
			UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Excluding[%d]: %s (ptr=%p) [%s]"),
				i, *Actor->GetName(), Actor, *Actor->GetClass()->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("ArenaManager: DestructionExcluded[%d] FAILED to load! Path=%s IsNull=%d"),
				i, *Ref.ToString(), Ref.IsNull());
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Total excluded actors: %d (from %d refs)"),
		ExcludedActors.Num(), DestructionExcluded.Num());
	for (const TSoftObjectPtr<AActor>& Ref : ExitBlockers)
	{
		if (AActor* Actor = Ref.LoadSynchronous())
		{
			ExcludedActors.Add(Actor);
		}
	}
	for (const TSoftObjectPtr<AArenaSpawnPoint>& Ref : SpawnPoints)
	{
		if (AActor* Actor = Ref.LoadSynchronous())
		{
			ExcludedActors.Add(Actor);
		}
	}
	if (AActor* Respawn = PlayerRespawnPoint.LoadSynchronous())
	{
		ExcludedActors.Add(Respawn);
	}
	if (AActor* Door = RewardDoor.LoadSynchronous())
	{
		ExcludedActors.Add(Door);
	}
	if (AActor* Container = RewardContainer.LoadSynchronous())
	{
		ExcludedActors.Add(Container);
	}
	if (AActor* Dummy = RewardDummy.LoadSynchronous())
	{
		ExcludedActors.Add(Dummy);
	}
	if (AActor* Island = LinkedIsland.LoadSynchronous())
	{
		ExcludedActors.Add(Island);
	}

	// Exclude the player
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			ExcludedActors.Add(Pawn);
		}
	}

	// Exclude alive NPCs
	for (const TWeakObjectPtr<AShooterNPC>& NPCPtr : AliveNPCs)
	{
		if (AShooterNPC* NPC = NPCPtr.Get())
		{
			ExcludedActors.Add(NPC);
		}
	}

	// Collect all actors within DestructionRadius via sphere overlap
	struct FDestructionTarget
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UStaticMeshComponent> MeshComp;
		float BoundsVolume;
		float DistanceFromEpicenter;
		bool bIsProp;
	};

	TArray<FDestructionTarget> Targets;
	const FVector ArenaCenter = GetActorLocation();

	TArray<FOverlapResult> Overlaps;
	FCollisionShape Sphere = FCollisionShape::MakeSphere(DestructionRadius);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	// Sweep WorldStatic + WorldDynamic to catch both static mesh actors and physics props
	GetWorld()->OverlapMultiByChannel(Overlaps, ArenaCenter, FQuat::Identity, ECC_WorldStatic, Sphere, QueryParams);

	TArray<FOverlapResult> DynamicOverlaps;
	GetWorld()->OverlapMultiByChannel(DynamicOverlaps, ArenaCenter, FQuat::Identity, ECC_WorldDynamic, Sphere, QueryParams);
	Overlaps.Append(DynamicOverlaps);

	TSet<AActor*> ProcessedActors;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (!Actor || ProcessedActors.Contains(Actor))
		{
			continue;
		}
		ProcessedActors.Add(Actor);

		if (ExcludedActors.Contains(Actor))
		{
			UE_LOG(LogTemp, Warning, TEXT("ArenaManager: EXCLUDED (in list): %s (ptr=%p) [%s]"),
				*Actor->GetName(), Actor, *Actor->GetClass()->GetName());
			continue;
		}

		// Skip world infrastructure: landscapes, brushes, volumes, level actors
		const FString ClassName = Actor->GetClass()->GetName();
		if (Actor->IsA(ABrush::StaticClass()) || Actor->IsA(AVolume::StaticClass())
			|| ClassName.Contains(TEXT("Landscape")))
		{
			UE_LOG(LogTemp, Warning, TEXT("ArenaManager: EXCLUDED (infrastructure): %s [%s]"),
				*Actor->GetName(), *Actor->GetClass()->GetName());
			continue;
		}

		// Skip projectiles — they are impact sources, not destruction targets
		if (Actor->IsA(AEMFProjectile::StaticClass()))
		{
			continue;
		}

		// Check if it's a prop
		AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(Actor);
		if (Prop && Prop->IsDead())
		{
			continue;
		}

		// Find StaticMeshComponent
		UStaticMeshComponent* MeshComp = nullptr;
		if (Prop)
		{
			MeshComp = Prop->PropMesh;
		}
		else
		{
			MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>();
		}

		if (!MeshComp || !MeshComp->GetStaticMesh())
		{
			continue;
		}

		// Calculate bounding volume
		const FBoxSphereBounds Bounds = MeshComp->CalcBounds(MeshComp->GetComponentTransform());
		const FVector Extent = Bounds.BoxExtent;
		const float Volume = Extent.X * Extent.Y * Extent.Z * 8.0f;

		const float Distance = FVector::Dist(Actor->GetActorLocation(), Epicenter);

		UE_LOG(LogTemp, Warning, TEXT("ArenaManager: TARGET: %s [%s] Mesh=%s Vol=%.0f Dist=%.0f"),
			*Actor->GetName(), *Actor->GetClass()->GetName(),
			*MeshComp->GetStaticMesh()->GetName(), Volume, Distance);

		FDestructionTarget Target;
		Target.Actor = Actor;
		Target.MeshComp = MeshComp;
		Target.BoundsVolume = Volume;
		Target.DistanceFromEpicenter = Distance;
		Target.bIsProp = (Prop != nullptr);
		Targets.Add(Target);
	}

	if (Targets.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ArenaManager: No destruction targets found within radius %.0f"), DestructionRadius);
		return;
	}

	// Sort by distance from epicenter (closest first — for shockwave timing)
	Targets.Sort([](const FDestructionTarget& A, const FDestructionTarget& B)
	{
		return A.DistanceFromEpicenter < B.DistanceFromEpicenter;
	});

	// Three-tier destruction (inspired by The Finals / VOID BREAKER):
	//   ZONE 1 (< GuaranteedRadius): Full GC tiling = rubble (mesh hidden)
	//   ZONE 2 (< CollapseRadius):   Physics collapse = mesh itself falls as whole piece
	//   ZONE 3 (< DestructionRadius): Survival chance based on LOS + distance
	const float SurvivalRange = DestructionRadius - DestructionCollapseRadius;
	int32 RubbleCount = 0;
	int32 CollapseCount = 0;
	int32 SurvivedCount = 0;
	int32 GCCount = 0;

	// Line-of-sight check params
	FCollisionQueryParams LOSParams;
	LOSParams.AddIgnoredActor(this);
	// Ignore the player
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			LOSParams.AddIgnoredActor(Pawn);
		}
	}

	for (int32 i = 0; i < Targets.Num(); i++)
	{
		const FDestructionTarget& Target = Targets[i];
		const float Dist = Target.DistanceFromEpicenter;

		// Line-of-sight check: can the explosion "see" this mesh?
		bool bHasLineOfSight = true;
		if (Target.Actor.IsValid())
		{
			FHitResult LOSHit;
			LOSParams.AddIgnoredActor(Target.Actor.Get());
			bHasLineOfSight = !GetWorld()->LineTraceSingleByChannel(
				LOSHit, Epicenter, Target.Actor->GetActorLocation(),
				ECC_WorldStatic, LOSParams);
			LOSParams.ClearIgnoredActors();
			LOSParams.AddIgnoredActor(this);
		}

		// Determine destruction tier
		// Zone 1 (Rubble): within GuaranteedRadius — always full GC tiling, no LOS required
		// Zone 2 (Collapse): within CollapseRadius — physics fall, LOS affects survival chance
		// Zone 3 (Survive): beyond CollapseRadius — distance + LOS based survival
		enum class ETier { Rubble, Collapse, Survive };
		ETier Tier;

		if (Dist <= DestructionGuaranteedRadius)
		{
			// Close enough = always rubble, regardless of LOS
			Tier = ETier::Rubble;
		}
		else if (Dist <= DestructionCollapseRadius)
		{
			// Objects behind cover in collapse zone have chance to survive
			if (!bHasLineOfSight && FMath::FRand() < 0.5f)
			{
				SurvivedCount++;
				continue;
			}
			Tier = ETier::Collapse;
		}
		else
		{
			// Beyond collapse radius: survival chance increases toward edge
			float SurvivalChance = 0.0f;
			if (SurvivalRange > KINDA_SMALL_NUMBER)
			{
				const float T = FMath::Clamp((Dist - DestructionCollapseRadius) / SurvivalRange, 0.0f, 1.0f);
				SurvivalChance = T * DestructionEdgeSurvivalChance;
			}
			// No line of sight = much higher survival
			if (!bHasLineOfSight)
			{
				SurvivalChance = FMath::Max(SurvivalChance, 0.7f);
			}

			if (SurvivalChance > 0.0f && FMath::FRand() < SurvivalChance)
			{
				SurvivedCount++;
				continue;
			}
			Tier = ETier::Collapse;
		}

		// Full GC only for rubble tier and within budget
		const bool bFullGC = (Tier == ETier::Rubble) && (GCCount < MaxFullDestructions);

		float Delay = 0.0f;
		if (DestructionWaveSpeed > 0.0f)
		{
			Delay = Dist / DestructionWaveSpeed;
		}

		// Capture by value for lambda safety
		TWeakObjectPtr<AActor> WeakActor = Target.Actor;
		TWeakObjectPtr<UStaticMeshComponent> WeakMesh = Target.MeshComp;
		bool bIsProp = Target.bIsProp;
		FVector EpicenterCopy = Epicenter;
		bool bCollapse = (Tier == ETier::Collapse);
		float LinDamp = DestructionLinearDamping;
		float AngDamp = DestructionAngularDamping;
		float GibLife = DestructionGibLifetime;
		float FreezeTime = DestructionGibFreezeTime;
		float CollapseImpulseStrength = CollapseImpulse;

		UNiagaraSystem* VFX = DestructionVFX;

		auto DestroyLambda = [this, WeakActor, WeakMesh, bFullGC, bCollapse, bIsProp, EpicenterCopy, LinDamp, AngDamp, GibLife, FreezeTime, CollapseImpulseStrength, VFX]()
		{
			AActor* Actor = WeakActor.Get();
			UStaticMeshComponent* MeshComp = WeakMesh.Get();
			if (!Actor || !MeshComp)
			{
				return;
			}

			if (bIsProp)
			{
				AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(Actor);
				if (Prop && !Prop->IsDead())
				{
					// Spawn VFX for props too (they visually break)
					if (VFX && GetWorld())
					{
						const FVector VFXLocation = MeshComp->Bounds.Origin;
						const float VFXScale = FMath::Max(1.0f, MeshComp->Bounds.BoxExtent.GetMax() / 100.0f);
						UNiagaraFunctionLibrary::SpawnSystemAtLocation(
							GetWorld(), VFX, VFXLocation, FRotator::ZeroRotator,
							FVector(VFXScale), true, true, ENCPoolMethod::AutoRelease);
					}
					if (bFullGC && !Prop->PropGeometryCollection)
					{
						SpawnDestructionGCForMesh(MeshComp, EpicenterCopy);
					}
					Prop->Explode(1.0f, 1.0f, 1.0f);
				}
			}
			else
			{
				if (bFullGC)
				{
					// RUBBLE: replace with GC cube tiling + dust VFX
					UE_LOG(LogTemp, Warning, TEXT("ArenaManager: RUBBLE lambda firing for %s, calling SpawnDestructionGCForMesh"),
						*Actor->GetName());
					if (VFX && GetWorld())
					{
						const FVector VFXLocation = MeshComp->Bounds.Origin;
						const float VFXScale = FMath::Max(1.0f, MeshComp->Bounds.BoxExtent.GetMax() / 100.0f);
						UNiagaraFunctionLibrary::SpawnSystemAtLocation(
							GetWorld(), VFX, VFXLocation, FRotator::ZeroRotator,
							FVector(VFXScale), true, true, ENCPoolMethod::AutoRelease);
					}

					SpawnDestructionGCForMesh(MeshComp, EpicenterCopy);
					Actor->SetActorHiddenInGame(true);
					MeshComp->SetSimulatePhysics(false);
					MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				}
				else if (bCollapse)
				{
					// COLLAPSE: enable physics on original mesh — it falls/topples as a whole piece
					UE_LOG(LogTemp, Warning, TEXT("ArenaManager: COLLAPSE lambda firing for %s, IsRoot=%d"),
						*Actor->GetName(), MeshComp == Actor->GetRootComponent());
					// Detach from any parent so it can move freely
					if (Actor->GetAttachParentActor())
					{
						Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
					}

					// For StaticMeshActor the root IS the mesh — don't detach root from itself
					if (MeshComp != Actor->GetRootComponent())
					{
						MeshComp->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
					}

					MeshComp->SetMobility(EComponentMobility::Movable);

					// CRITICAL: must recreate physics state after mobility change
					// otherwise physics body doesn't exist and SetSimulatePhysics silently fails
					MeshComp->RecreatePhysicsState();

					MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
					MeshComp->SetCollisionObjectType(ECC_PhysicsBody);
					MeshComp->SetCollisionResponseToAllChannels(ECR_Block);
					MeshComp->SetSimulatePhysics(true);
					MeshComp->SetEnableGravity(true);
					MeshComp->WakeAllRigidBodies();
					MeshComp->SetLinearDamping(LinDamp * 2.0f);
					MeshComp->SetAngularDamping(AngDamp * 2.0f);

					// Delay impulse by 1 tick so physics body is fully initialized
					TWeakObjectPtr<UStaticMeshComponent> WeakImpulseMesh = MeshComp;
					FVector CollapseEpicenter = EpicenterCopy;
					float ImpulseStr = CollapseImpulseStrength;
					GetWorldTimerManager().SetTimerForNextTick(
						FTimerDelegate::CreateLambda([WeakImpulseMesh, CollapseEpicenter, ImpulseStr]()
						{
							if (UStaticMeshComponent* MC = WeakImpulseMesh.Get())
							{
								MC->WakeAllRigidBodies();
								// Push away from epicenter + downward to knock off balance
								const FVector Dir = (MC->GetComponentLocation() - CollapseEpicenter).GetSafeNormal();
								const FVector Impulse = (Dir * 0.4f + FVector::DownVector * 0.6f) * ImpulseStr;
								MC->AddImpulse(Impulse, NAME_None, true);
							}
						}));

					// Freeze physics after settling — mesh stays where it fell
					if (FreezeTime > 0.0f)
					{
						TWeakObjectPtr<UStaticMeshComponent> WeakCollapseMesh = MeshComp;
						FTimerHandle CollapseFreeze;
						GetWorldTimerManager().SetTimer(CollapseFreeze,
							FTimerDelegate::CreateLambda([WeakCollapseMesh]()
							{
								if (UStaticMeshComponent* MC = WeakCollapseMesh.Get())
								{
									MC->SetSimulatePhysics(false);
								}
							}),
							FreezeTime, false);
						DestructionTimerHandles.Add(CollapseFreeze);
					}

					// Auto-destroy after lifespan (0 = persist forever)
					if (GibLife > 0.0f)
					{
						Actor->SetLifeSpan(GibLife * 2.0f);
					}
				}
				else
				{
					// HIDE: just disappear (over GC budget)
					Actor->SetActorHiddenInGame(true);
					MeshComp->SetSimulatePhysics(false);
					MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				}
			}
		};

		if (bFullGC) GCCount++;
		if (bCollapse) CollapseCount++; else RubbleCount++;

		if (Delay <= KINDA_SMALL_NUMBER)
		{
			DestroyLambda();
		}
		else
		{
			FTimerHandle Handle;
			GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(MoveTemp(DestroyLambda)), Delay, false);
			DestructionTimerHandles.Add(Handle);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Destruction — %d rubble (%d GC), %d collapse, %d survived / %d total"),
		RubbleCount, GCCount, CollapseCount, SurvivedCount, Targets.Num());
}

void AArenaManager::SpawnDestructionGCForMesh(UStaticMeshComponent* MeshComp, const FVector& Epicenter)
{
	if (!DestructionGC || !MeshComp || !MeshComp->GetStaticMesh() || !GetWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("ArenaManager::SpawnDestructionGCForMesh EARLY EXIT — GC=%d Mesh=%d StaticMesh=%d World=%d"),
			DestructionGC != nullptr, MeshComp != nullptr,
			MeshComp ? (MeshComp->GetStaticMesh() != nullptr) : false,
			GetWorld() != nullptr);
		return;
	}

	const FTransform MeshTransform = MeshComp->GetComponentTransform();
	const FVector MeshLocation = MeshTransform.GetLocation();
	const FQuat MeshQuat = MeshTransform.GetRotation();
	const FRotator MeshRotation = MeshQuat.Rotator();

	// Get mesh world-space extents
	const FBox MeshLocalBox = MeshComp->GetStaticMesh()->GetBoundingBox();
	const FVector MeshLocalExtent = MeshLocalBox.GetExtent();
	const FVector MeshWorldScale = MeshTransform.GetScale3D();
	const FVector MeshWorldExtent = MeshLocalExtent * MeshWorldScale;

	// Tile size = full size of one GC cube (diameter, not radius)
	const FVector TileSize = DestructionGCHalfExtent * 2.0f;

	// How many tiles fit along each axis
	int32 CountX = FMath::Max(1, FMath::CeilToInt(MeshWorldExtent.X * 2.0f / TileSize.X));
	int32 CountY = FMath::Max(1, FMath::CeilToInt(MeshWorldExtent.Y * 2.0f / TileSize.Y));
	int32 CountZ = FMath::Max(1, FMath::CeilToInt(MeshWorldExtent.Z * 2.0f / TileSize.Z));

	int32 TotalTiles = CountX * CountY * CountZ;

	// If over budget, uniformly reduce counts
	if (TotalTiles > MaxGCTilesPerMesh)
	{
		const float ReductionFactor = FMath::Pow(static_cast<float>(MaxGCTilesPerMesh) / TotalTiles, 1.0f / 3.0f);
		CountX = FMath::Max(1, FMath::RoundToInt(CountX * ReductionFactor));
		CountY = FMath::Max(1, FMath::RoundToInt(CountY * ReductionFactor));
		CountZ = FMath::Max(1, FMath::RoundToInt(CountZ * ReductionFactor));
		TotalTiles = CountX * CountY * CountZ;
	}

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager: GC tiling for %s — WorldExtent=%s TileSize=%s Grid=%dx%dx%d=%d tiles"),
		*MeshComp->GetStaticMesh()->GetName(), *MeshWorldExtent.ToString(), *TileSize.ToString(),
		CountX, CountY, CountZ, TotalTiles);

	// Copy first material from mesh
	UMaterialInterface* MeshMat = nullptr;
	if (MeshComp->GetNumMaterials() > 0)
	{
		MeshMat = MeshComp->GetMaterial(0);
	}

	// Compute the grid origin in local mesh space (bottom-left-back corner)
	// Grid is centered on mesh center, each tile occupies TileSize
	const FVector GridOriginLocal = FVector(
		-CountX * TileSize.X * 0.5f + TileSize.X * 0.5f,
		-CountY * TileSize.Y * 0.5f + TileSize.Y * 0.5f,
		-CountZ * TileSize.Z * 0.5f + TileSize.Z * 0.5f
	);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	int32 SpawnedCount = 0;

	for (int32 ix = 0; ix < CountX; ix++)
	{
		for (int32 iy = 0; iy < CountY; iy++)
		{
			for (int32 iz = 0; iz < CountZ; iz++)
			{
				// Local offset for this tile + random jitter to break grid pattern
				const float Jitter = DestructionGCHalfExtent.X * 0.4f;
				const FVector LocalOffset(
					GridOriginLocal.X + ix * TileSize.X + FMath::RandRange(-Jitter, Jitter),
					GridOriginLocal.Y + iy * TileSize.Y + FMath::RandRange(-Jitter, Jitter),
					GridOriginLocal.Z + iz * TileSize.Z + FMath::RandRange(-Jitter, Jitter)
				);

				// Transform to world space using mesh rotation
				const FVector WorldPos = MeshLocation + MeshQuat.RotateVector(LocalOffset);

				AGeometryCollectionActor* GCActor = GetWorld()->SpawnActor<AGeometryCollectionActor>(
					WorldPos, MeshRotation, SpawnParams);
				if (!GCActor) continue;

				UGeometryCollectionComponent* GCComp = GCActor->GetGeometryCollectionComponent();
				if (!GCComp) { GCActor->Destroy(); continue; }

				// Native scale — NO non-uniform scaling
				GCActor->SetActorScale3D(FVector::OneVector);

				GCComp->SetCollisionObjectType(ECC_PhysicsBody);
				GCComp->SetCollisionResponseToAllChannels(ECR_Ignore);
				GCComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
				GCComp->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);

				GCComp->SetRestCollection(DestructionGC);

				if (MeshMat)
				{
					GCComp->SetMaterial(0, MeshMat);
				}

				GCComp->SetLinearDamping(DestructionLinearDamping);
				GCComp->SetAngularDamping(DestructionAngularDamping);

				GCComp->SetSimulatePhysics(true);
				GCComp->RecreatePhysicsState();

				// Break all clusters
				UUniformScalar* StrainField = NewObject<UUniformScalar>(GCActor);
				StrainField->Magnitude = 999999.0f;
				GCComp->ApplyPhysicsField(true,
					EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
					nullptr, StrainField);

				// Scatter debris outward from epicenter (DestructionImpulse, default 50)
				if (DestructionImpulse > 0.0f)
				{
					URadialVector* RadialVelocity = NewObject<URadialVector>(GCActor);
					RadialVelocity->Magnitude = DestructionImpulse;
					RadialVelocity->Position = Epicenter;
					GCComp->ApplyPhysicsField(true,
						EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity,
						nullptr, RadialVelocity);
				}

				if (DestructionAngularImpulse > 0.0f)
				{
					URadialVector* AngularVelocity = NewObject<URadialVector>(GCActor);
					AngularVelocity->Magnitude = DestructionAngularImpulse;
					AngularVelocity->Position = Epicenter;
					GCComp->ApplyPhysicsField(true,
						EGeometryCollectionPhysicsTypeEnum::Chaos_AngularVelocity,
						nullptr, AngularVelocity);
				}

				if (DestructionGibLifetime > 0.0f)
				{
					GCActor->SetLifeSpan(DestructionGibLifetime);
				}

				// Schedule physics freeze — once debris settles, disable simulation
				// to stop Chaos solver from wasting CPU on sleeping fragments
				if (DestructionGibFreezeTime > 0.0f)
				{
					TWeakObjectPtr<UGeometryCollectionComponent> WeakGC = GCComp;
					FTimerHandle FreezeHandle;
					GetWorldTimerManager().SetTimer(FreezeHandle,
						FTimerDelegate::CreateLambda([WeakGC]()
						{
							if (UGeometryCollectionComponent* GC = WeakGC.Get())
							{
								GC->SetSimulatePhysics(false);
							}
						}),
						DestructionGibFreezeTime, false);
					DestructionTimerHandles.Add(FreezeHandle);
				}

				SpawnedCount++;
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Tiled %d GC cubes for %s (grid %dx%dx%d, mesh extent %s)"),
		SpawnedCount, *MeshComp->GetStaticMesh()->GetName(),
		CountX, CountY, CountZ, *MeshWorldExtent.ToString());
}

// ==================== Camera Lock ====================

void AArenaManager::StartCameraLock(const FVector& Epicenter)
{
	if (CameraLockDuration <= 0.0f || !GetWorld())
	{
		return;
	}

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}

	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		return;
	}

	const FVector PlayerLocation = Pawn->GetActorLocation();
	const FVector EyeLocation = PlayerLocation + FVector(0.0f, 0.0f, 64.0f);

	// Check LOS from player to epicenter
	FHitResult LOSHit;
	FCollisionQueryParams LOSParams;
	LOSParams.AddIgnoredActor(this);
	LOSParams.AddIgnoredActor(Pawn);

	bool bHasLOS = !GetWorld()->LineTraceSingleByChannel(
		LOSHit, EyeLocation, Epicenter, ECC_WorldStatic, LOSParams);

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager: CameraLock LOS check — HasLOS=%d, PlayerPos=%s, Epicenter=%s"),
		bHasLOS, *PlayerLocation.ToString(), *Epicenter.ToString());
	if (!bHasLOS)
	{
		UE_LOG(LogTemp, Warning, TEXT("ArenaManager: LOS blocked by %s at %s"),
			LOSHit.GetActor() ? *LOSHit.GetActor()->GetName() : TEXT("NULL"),
			*LOSHit.ImpactPoint.ToString());
	}

	if (!bHasLOS)
	{
		// No line of sight — find closest NavMesh point NEAR THE PLAYER with visibility to epicenter
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (NavSys)
		{
			FVector BestPoint = PlayerLocation;
			float BestDist = MAX_FLT;
			bool bFoundPoint = false;
			int32 NavHits = 0;
			int32 LOSHits = 0;

			// Search around the player, expanding radius if needed
			const float SearchRadii[] = { 1500.0f, 3000.0f, 5000.0f };
			for (float SearchRadius : SearchRadii)
			{
				for (int32 Attempt = 0; Attempt < 30; Attempt++)
				{
					FNavLocation NavResult;
					if (NavSys->GetRandomReachablePointInRadius(PlayerLocation, SearchRadius, NavResult))
					{
						NavHits++;

						// Check LOS from candidate point to epicenter
						FVector CandidateEye = NavResult.Location + FVector(0.0f, 0.0f, 64.0f);

						FCollisionQueryParams CandidateParams;
						CandidateParams.AddIgnoredActor(this);
						CandidateParams.AddIgnoredActor(Pawn);

						FHitResult CandidateHit;
						if (!GetWorld()->LineTraceSingleByChannel(
							CandidateHit, CandidateEye, Epicenter, ECC_WorldStatic, CandidateParams))
						{
							LOSHits++;
							// Has LOS — check if it's the closest to player
							float Dist = FVector::Dist(NavResult.Location, PlayerLocation);
							if (Dist < BestDist)
							{
								BestDist = Dist;
								BestPoint = NavResult.Location;
								bFoundPoint = true;
							}
						}
					}
				}

				// If we found a good point, stop expanding
				if (bFoundPoint) break;
			}

			UE_LOG(LogTemp, Warning, TEXT("ArenaManager: NavMesh search — %d nav hits, %d with LOS, found=%d"),
				NavHits, LOSHits, bFoundPoint);

			if (bFoundPoint)
			{
				UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Teleporting player to LOS point at %s (dist=%.0f from original pos)"),
					*BestPoint.ToString(), BestDist);
				Pawn->SetActorLocation(BestPoint);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("ArenaManager: No NavMesh point with LOS found after searching all radii, locking camera from current position"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("ArenaManager: No NavigationSystem available for teleport!"));
		}
	}

	// Lock camera
	CameraLockTarget = Epicenter;
	bCameraLocked = true;

	// Disable player look + move input
	PC->SetIgnoreLookInput(true);
	PC->SetIgnoreMoveInput(true);

	// Start per-frame camera update timer (~60fps)
	GetWorldTimerManager().SetTimer(CameraLockUpdateHandle,
		this, &AArenaManager::UpdateCameraLock, 0.016f, true);

	// Schedule unlock after duration
	GetWorldTimerManager().SetTimer(CameraLockEndHandle,
		this, &AArenaManager::EndCameraLock, CameraLockDuration, false);

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Camera locked on epicenter for %.1f seconds"), CameraLockDuration);
}

void AArenaManager::UpdateCameraLock()
{
	if (!bCameraLocked || !GetWorld())
	{
		return;
	}

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->GetPawn())
	{
		return;
	}

	const FVector EyeLocation = PC->GetPawn()->GetActorLocation() + FVector(0.0f, 0.0f, 64.0f);
	const FVector ToTarget = CameraLockTarget - EyeLocation;
	const FRotator TargetRotation = ToTarget.Rotation();

	// Smooth interpolation — higher InterpSpeed = faster blend
	const float InterpSpeed = (CameraBlendTime > KINDA_SMALL_NUMBER) ? (1.0f / CameraBlendTime) : 100.0f;
	const FRotator Current = PC->GetControlRotation();
	const FRotator NewRotation = FMath::RInterpTo(Current, TargetRotation, 0.016f, InterpSpeed);

	PC->SetControlRotation(NewRotation);
}

void AArenaManager::EndCameraLock()
{
	if (!bCameraLocked)
	{
		return;
	}

	bCameraLocked = false;

	GetWorldTimerManager().ClearTimer(CameraLockUpdateHandle);
	GetWorldTimerManager().ClearTimer(CameraLockEndHandle);

	if (GetWorld())
	{
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			// Reset (not just decrement) — clears the ignore stack completely
			PC->ResetIgnoreLookInput();
			PC->ResetIgnoreMoveInput();
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Camera unlocked"));
}

// ==================== Island ====================

void AArenaManager::ForceCompleteArena()
{
	if (CurrentState == EArenaState::Completed || CurrentState == EArenaState::Idle)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("ArenaManager: ForceCompleteArena — killing all NPCs and completing"));

	// Kill all remaining NPCs via lethal damage (triggers normal death flow: ragdoll, OnNPCDeath, etc.)
	for (const TWeakObjectPtr<AShooterNPC>& NPCPtr : AliveNPCs)
	{
		if (AShooterNPC* NPC = NPCPtr.Get())
		{
			UGameplayStatics::ApplyDamage(NPC, 99999.f, nullptr, nullptr, UDamageType::StaticClass());
		}
	}

	// Clear wave timer (might be between waves)
	GetWorldTimerManager().ClearTimer(WaveTimerHandle);

	CompleteArena();
}

void AArenaManager::OnLinkedIslandDestroyed(ADestructibleIslandActor* Island, AActor* Destroyer)
{
	UE_LOG(LogTemp, Warning, TEXT("ArenaManager: Linked island destroyed — force completing arena"));
	ForceCompleteArena();
}

// ==================== Reward Container ====================

void AArenaManager::OnRewardDummyDeath(AShooterDummy* Dummy, AActor* Killer)
{
	if (ARewardContainer* Container = RewardContainer.Get())
	{
		Container->Activate();
		UE_LOG(LogTemp, Log, TEXT("ArenaManager: Reward dummy died — activating RewardContainer"));
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
