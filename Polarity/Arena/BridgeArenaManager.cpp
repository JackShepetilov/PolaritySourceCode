// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "BridgeArenaManager.h"
#include "ArenaSpawnPoint.h"
#include "ArenaWaveData.h"
#include "Components/SplineComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

ABridgeArenaManager::ABridgeArenaManager()
{
	// Bridge variant needs a tick to drive periodic spawn pressure.
	// Base ArenaManager keeps tick off; we re-enable here for this subclass only.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ABridgeArenaManager::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* SplineOwner = BridgeSplineActor.LoadSynchronous())
	{
		CachedSpline = SplineOwner->FindComponentByClass<USplineComponent>();
		if (CachedSpline)
		{
			CachedSplineLength = CachedSpline->GetSplineLength();
			UE_LOG(LogTemp, Warning, TEXT("BridgeArenaManager: Spline length %.0f cm"), CachedSplineLength);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("BridgeArenaManager: BridgeSplineActor %s has no SplineComponent"),
				*SplineOwner->GetName());
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("BridgeArenaManager: BridgeSplineActor is not set — bridge filtering will not work"));
	}
}

void ABridgeArenaManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Only drive periodic spawns while the arena is in active combat
	if (CurrentState != EArenaState::Active || ArenaMode != EArenaMode::Sustain)
	{
		return;
	}

	if (!CachedSpline || CachedSplineLength <= 0.0f)
	{
		return;
	}

	RefreshPlayerProjectionCache();

	// Past the end-zone cutoff — leave the button area clear
	if (CachedPlayerProgress01 >= MaxProgressToSpawn)
	{
		return;
	}

	TimeSinceLastBridgeSpawn += DeltaTime;
	const float CurrentInterval = ComputeCurrentSpawnInterval();
	if (TimeSinceLastBridgeSpawn < CurrentInterval)
	{
		return;
	}

	// Only add pressure if we're below the (also-growing) cap
	const int32 AliveCount = GetAliveNPCs().Num();
	const int32 Cap = GetEffectiveMaxSustainEnemies();
	if (AliveCount >= Cap)
	{
		// Hold the accumulator so the next slot below cap fires immediately
		TimeSinceLastBridgeSpawn = CurrentInterval;
		return;
	}

	TimeSinceLastBridgeSpawn = 0.0f;
	SpawnSustainEnemy();
}

float ABridgeArenaManager::GetPlayerProgress01() const
{
	if (!CachedSpline || CachedSplineLength <= 0.0f)
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
	if (!CachedSpline)
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

	if (!CachedSpline || CachedSplineLength <= 0.0f)
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

	const float PointDistanceAlongSpline = CachedSpline->GetDistanceAlongSplineAtLocation(
		Point->GetActorLocation(), ESplineCoordinateSpace::World);

	const float DistanceAhead = PointDistanceAlongSpline - CachedPlayerDistanceAlongSpline;

	if (DistanceAhead < MinDistanceAhead)
	{
		return false;
	}
	if (DistanceAhead > MaxDistanceAhead)
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
	if (!CachedSpline || CachedSplineLength <= 0.0f)
	{
		return 0.0f;
	}

	const float Distance = CachedSpline->GetDistanceAlongSplineAtLocation(
		WorldLocation, ESplineCoordinateSpace::World);

	return FMath::Clamp(Distance / CachedSplineLength, 0.0f, 1.0f);
}

void ABridgeArenaManager::RefreshPlayerProjectionCache() const
{
	if (!CachedSpline || CachedSplineLength <= 0.0f)
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
	CachedPlayerDistanceAlongSpline = CachedSpline->GetDistanceAlongSplineAtLocation(
		PlayerLoc, ESplineCoordinateSpace::World);
	CachedPlayerProgress01 = FMath::Clamp(CachedPlayerDistanceAlongSpline / CachedSplineLength, 0.0f, 1.0f);
}
