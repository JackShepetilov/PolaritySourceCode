// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "BridgeArenaManager.h"
#include "ArenaSpawnPoint.h"
#include "ArenaWaveData.h"
#include "Components/SplineComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "EngineUtils.h"

// File-local throttle for Tick-summary debug logs (single bridge per level expected).
// Kept here instead of as a member to avoid touching the header for pure debug state.
namespace { float GBridgeDebugLogAccumulator = 0.0f; }

ABridgeArenaManager::ABridgeArenaManager()
{
	// Bridge variant needs a tick to drive periodic spawn pressure.
	// Base ArenaManager keeps tick off; we re-enable here for this subclass only.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// Build the spline directly into the manager — editing the bridge path is just editing
	// this actor in the level, no extra actors to wire up.
	BridgeSpline = CreateDefaultSubobject<USplineComponent>(TEXT("BridgeSpline"));
	if (BridgeSpline)
	{
		BridgeSpline->SetupAttachment(GetRootComponent());
	}
}

void ABridgeArenaManager::BeginPlay()
{
	Super::BeginPlay();

	if (BridgeSpline)
	{
		CachedSplineLength = BridgeSpline->GetSplineLength();
		UE_LOG(LogTemp, Warning, TEXT("[BRIDGE_DEBUG] BeginPlay: Spline length %.0f cm (%d points)"),
			CachedSplineLength, BridgeSpline->GetNumberOfSplinePoints());

		if (CachedSplineLength <= 0.0f)
		{
			UE_LOG(LogTemp, Error, TEXT("[BRIDGE_DEBUG] BeginPlay: Spline has zero length — add at least two points in the editor"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[BRIDGE_DEBUG] BeginPlay: BridgeSpline component is null"));
	}

	// Auto-collect spawn points from our own level before logging the dump, so the dump reflects
	// the final state. Designers can disable the flag if they want full manual control.
	if (bAutoCollectSpawnPointsFromOwnLevel)
	{
		const int32 BeforeCount = SpawnPoints.Num();
		const int32 Added = CollectSpawnPointsFromOwnLevel();
		UE_LOG(LogTemp, Warning, TEXT("[BRIDGE_DEBUG] BeginPlay: Auto-collected %d spawn points (had %d manually, total %d)"),
			Added, BeforeCount, SpawnPoints.Num());
	}

	UE_LOG(LogTemp, Warning, TEXT("[BRIDGE_DEBUG] BeginPlay: SpawnPoints=%d, SustainPool=%d, ArenaMode=%d (1=Sustain), CanEverTick=%d, TickEnabled=%d"),
		SpawnPoints.Num(),
		SustainEnemyPool.Num(),
		(int32)ArenaMode,
		PrimaryActorTick.bCanEverTick ? 1 : 0,
		IsActorTickEnabled() ? 1 : 0);

	UE_LOG(LogTemp, Warning, TEXT("[BRIDGE_DEBUG] BeginPlay: MaxEnemiesAtStart=%d, MaxEnemiesAtEnd=%d, SpawnInterval %.1f→%.1f, Distance %.0f→%.0f, MaxProgress %.2f"),
		MaxEnemiesAtStart, MaxEnemiesAtEnd,
		SpawnIntervalAtStart, SpawnIntervalAtEnd,
		MinDistanceAhead, MaxDistanceAhead,
		MaxProgressToSpawn);

	// Dump spawn point status so we can see what BP collected
	for (int32 i = 0; i < SpawnPoints.Num(); ++i)
	{
		const TSoftObjectPtr<AArenaSpawnPoint>& Ref = SpawnPoints[i];
		AArenaSpawnPoint* SP = Ref.Get();
		UE_LOG(LogTemp, Warning, TEXT("[BRIDGE_DEBUG]   SpawnPoint[%d]: %s (path=%s)"),
			i,
			SP ? *SP->GetName() : TEXT("UNRESOLVED"),
			*Ref.ToSoftObjectPath().ToString());
	}
}

void ABridgeArenaManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Throttle Tick logging to once per second
	GBridgeDebugLogAccumulator += DeltaTime;
	const bool bShouldLog = GBridgeDebugLogAccumulator >= 1.0f;
	if (bShouldLog)
	{
		GBridgeDebugLogAccumulator = 0.0f;
	}

	// Only drive periodic spawns while the arena is in active combat
	if (CurrentState != EArenaState::Active || ArenaMode != EArenaMode::Sustain)
	{
		if (bShouldLog)
		{
			UE_LOG(LogTemp, Warning, TEXT("[BRIDGE_DEBUG] Tick SKIP: State=%d (need 1=Active), Mode=%d (need 1=Sustain)"),
				(int32)CurrentState, (int32)ArenaMode);
		}
		return;
	}

	if (!BridgeSpline || CachedSplineLength <= 0.0f)
	{
		if (bShouldLog)
		{
			UE_LOG(LogTemp, Warning, TEXT("[BRIDGE_DEBUG] Tick SKIP: Spline invalid (Spline=%p, Length=%.1f)"),
				BridgeSpline.Get(), CachedSplineLength);
		}
		return;
	}

	RefreshPlayerProjectionCache();

	// Past the end-zone cutoff — leave the button area clear
	if (CachedPlayerProgress01 >= MaxProgressToSpawn)
	{
		if (bShouldLog)
		{
			UE_LOG(LogTemp, Warning, TEXT("[BRIDGE_DEBUG] Tick SKIP: Player past MaxProgress (%.2f >= %.2f)"),
				CachedPlayerProgress01, MaxProgressToSpawn);
		}
		return;
	}

	TimeSinceLastBridgeSpawn += DeltaTime;
	const float CurrentInterval = ComputeCurrentSpawnInterval();

	// Only add pressure if we're below the (also-growing) cap
	const int32 AliveCount = GetAliveNPCs().Num();
	const int32 Cap = GetEffectiveMaxSustainEnemies();

	if (bShouldLog)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BRIDGE_DEBUG] Tick: Progress=%.2f, PlayerDist=%.0f, Interval=%.2fs (accum=%.2fs), Alive=%d, Cap=%d, SpawnPoints=%d, SustainPool=%d"),
			CachedPlayerProgress01, CachedPlayerDistanceAlongSpline,
			CurrentInterval, TimeSinceLastBridgeSpawn,
			AliveCount, Cap, SpawnPoints.Num(), SustainEnemyPool.Num());
	}

	if (TimeSinceLastBridgeSpawn < CurrentInterval)
	{
		return;
	}

	if (AliveCount >= Cap)
	{
		// Hold the accumulator so the next slot below cap fires immediately
		TimeSinceLastBridgeSpawn = CurrentInterval;
		if (bShouldLog)
		{
			UE_LOG(LogTemp, Warning, TEXT("[BRIDGE_DEBUG] Tick: at cap (%d/%d), waiting for kill"), AliveCount, Cap);
		}
		return;
	}

	TimeSinceLastBridgeSpawn = 0.0f;
	UE_LOG(LogTemp, Warning, TEXT("[BRIDGE_DEBUG] Tick: ATTEMPTING SPAWN (progress=%.2f, alive=%d/%d, interval=%.2fs)"),
		CachedPlayerProgress01, AliveCount, Cap, CurrentInterval);
	SpawnSustainEnemy();
}

float ABridgeArenaManager::GetPlayerProgress01() const
{
	if (!BridgeSpline || CachedSplineLength <= 0.0f)
	{
		return 0.0f;
	}

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
	if (!PlayerPawn)
	{
		return 0.0f;
	}

	return ProjectToBridge01(PlayerPawn->GetActorLocation());
}

int32 ABridgeArenaManager::GetEffectiveMaxSustainEnemies() const
{
	if (!BridgeSpline)
	{
		// No spline → fall back to base behavior so editor playtesting without a spline still works
		return Super::GetEffectiveMaxSustainEnemies();
	}

	const float Progress = FMath::Clamp(GetPlayerProgress01(), 0.0f, 1.0f);
	const float Lerped = FMath::Lerp(
		static_cast<float>(MaxEnemiesAtStart),
		static_cast<float>(MaxEnemiesAtEnd),
		Progress);
	return FMath::Max(0, FMath::RoundToInt(Lerped));
}

bool ABridgeArenaManager::IsSpawnPointAvailable(AArenaSpawnPoint* Point) const
{
	if (!Super::IsSpawnPointAvailable(Point))
	{
		return false;
	}

	if (!BridgeSpline || CachedSplineLength <= 0.0f)
	{
		// Fall through — without a spline we can't filter by position
		return true;
	}

	// Use cached projection if Tick is running; otherwise refresh now (e.g. SpawnSustainEnemy
	// called externally without Tick having run yet this frame).
	if (CachedPlayerDistanceAlongSpline <= 0.0f && CachedPlayerProgress01 <= 0.0f)
	{
		RefreshPlayerProjectionCache();
	}

	const float PointDistanceAlongSpline = BridgeSpline->GetDistanceAlongSplineAtLocation(
		Point->GetActorLocation(), ESplineCoordinateSpace::World);

	const float DistanceAhead = PointDistanceAlongSpline - CachedPlayerDistanceAlongSpline;

	const bool bTooClose = DistanceAhead < MinDistanceAhead;
	const bool bTooFar = DistanceAhead > MaxDistanceAhead;

	UE_LOG(LogTemp, Warning,
		TEXT("[BRIDGE_DEBUG]   IsSpawnPointAvailable %s: SP_dist=%.0f, Player_dist=%.0f, Ahead=%.0f, Range=[%.0f..%.0f] → %s"),
		*Point->GetName(),
		PointDistanceAlongSpline, CachedPlayerDistanceAlongSpline, DistanceAhead,
		MinDistanceAhead, MaxDistanceAhead,
		bTooClose ? TEXT("REJECT_TOO_CLOSE") : (bTooFar ? TEXT("REJECT_TOO_FAR") : TEXT("OK")));

	if (bTooClose || bTooFar)
	{
		return false;
	}
	return true;
}

float ABridgeArenaManager::ComputeCurrentSpawnInterval() const
{
	const float Progress = FMath::Clamp(GetPlayerProgress01(), 0.0f, 1.0f);
	return FMath::Lerp(SpawnIntervalAtStart, SpawnIntervalAtEnd, Progress);
}

float ABridgeArenaManager::ProjectToBridge01(const FVector& WorldLocation) const
{
	if (!BridgeSpline || CachedSplineLength <= 0.0f)
	{
		return 0.0f;
	}

	const float Distance = BridgeSpline->GetDistanceAlongSplineAtLocation(
		WorldLocation, ESplineCoordinateSpace::World);

	return FMath::Clamp(Distance / CachedSplineLength, 0.0f, 1.0f);
}

void ABridgeArenaManager::RefreshPlayerProjectionCache() const
{
	if (!BridgeSpline || CachedSplineLength <= 0.0f)
	{
		CachedPlayerProgress01 = 0.0f;
		CachedPlayerDistanceAlongSpline = 0.0f;
		return;
	}

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
	if (!PlayerPawn)
	{
		CachedPlayerProgress01 = 0.0f;
		CachedPlayerDistanceAlongSpline = 0.0f;
		return;
	}

	const FVector PlayerLoc = PlayerPawn->GetActorLocation();
	CachedPlayerDistanceAlongSpline = BridgeSpline->GetDistanceAlongSplineAtLocation(
		PlayerLoc, ESplineCoordinateSpace::World);
	CachedPlayerProgress01 = FMath::Clamp(CachedPlayerDistanceAlongSpline / CachedSplineLength, 0.0f, 1.0f);
}

int32 ABridgeArenaManager::CollectSpawnPointsFromOwnLevel()
{
	ULevel* MyLevel = GetLevel();
	if (!MyLevel)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BRIDGE_DEBUG] CollectSpawnPointsFromOwnLevel: GetLevel() returned null"));
		return 0;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}

	const FString MyLevelName = MyLevel->GetOuter() ? MyLevel->GetOuter()->GetName() : TEXT("UNKNOWN");
	int32 Added = 0;
	int32 SkippedOtherLevel = 0;
	int32 SkippedDuplicate = 0;

	for (TActorIterator<AArenaSpawnPoint> It(World); It; ++It)
	{
		AArenaSpawnPoint* SP = *It;
		if (!SP)
		{
			continue;
		}

		if (SP->GetLevel() != MyLevel)
		{
			++SkippedOtherLevel;
			continue;
		}

		// Manual de-dup against existing SoftObjectPtrs (designer may have hand-added some)
		bool bAlreadyPresent = false;
		for (const TSoftObjectPtr<AArenaSpawnPoint>& Existing : SpawnPoints)
		{
			if (Existing.Get() == SP)
			{
				bAlreadyPresent = true;
				break;
			}
		}
		if (bAlreadyPresent)
		{
			++SkippedDuplicate;
			continue;
		}

		SpawnPoints.Add(TSoftObjectPtr<AArenaSpawnPoint>(SP));
		++Added;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[BRIDGE_DEBUG] CollectSpawnPointsFromOwnLevel(level=%s): added=%d, skipped_other_level=%d, skipped_duplicate=%d"),
		*MyLevelName, Added, SkippedOtherLevel, SkippedDuplicate);

	return Added;
}
