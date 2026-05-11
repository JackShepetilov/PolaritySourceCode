// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArenaManager.h"
#include "BridgeArenaManager.generated.h"

class USplineComponent;

/**
 * Bridge-specific arena variant.
 *
 * Drives a "running gauntlet" along a spline:
 *  - Enemies only spawn ahead of the player along the spline axis (configurable distance band).
 *  - Spawn cap and tick-based spawn rate both lerp from start values to end values as the
 *    player progresses along the spline (0.0 to 1.0).
 *  - Designed for Sustain mode — the reactive kill→respawn loop is preserved; the periodic
 *    Tick spawner adds on top of it so pressure grows even when the player isn't killing.
 *  - At the far end, a button (wired in BP) plays a Level Sequence and calls KillAllAliveNPCs
 *    to wipe the bridge cinematically.
 */
UCLASS(Blueprintable)
class POLARITY_API ABridgeArenaManager : public AArenaManager
{
	GENERATED_BODY()

public:
	ABridgeArenaManager();

	// ==================== Spline ====================

	/** Actor containing the USplineComponent that defines the bridge axis.
	 *  The first spline point is "start" (progress 0), the last is "end" (progress 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Spline")
	TSoftObjectPtr<AActor> BridgeSplineActor;

	// ==================== Spawn Rate ====================

	/** Seconds between periodic spawn attempts when player is at the start of the bridge */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Spawn", meta = (Units = "s", ClampMin = "0.1"))
	float SpawnIntervalAtStart = 4.0f;

	/** Seconds between periodic spawn attempts when player has reached the far end */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Spawn", meta = (Units = "s", ClampMin = "0.1"))
	float SpawnIntervalAtEnd = 0.7f;

	// ==================== Spawn Cap (lerps with progress) ====================

	/** Max concurrent enemies allowed at the start of the bridge */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Spawn", meta = (ClampMin = "0"))
	int32 MaxEnemiesAtStart = 2;

	/** Max concurrent enemies allowed when player reaches the far end */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Spawn", meta = (ClampMin = "0"))
	int32 MaxEnemiesAtEnd = 7;

	// ==================== Ahead-of-Player Filtering ====================

	/** Don't spawn closer than this distance ahead of the player along the spline (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Spawn", meta = (Units = "cm", ClampMin = "0.0"))
	float MinDistanceAhead = 1500.0f;

	/** Don't spawn farther than this distance ahead of the player along the spline (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Spawn", meta = (Units = "cm", ClampMin = "0.0"))
	float MaxDistanceAhead = 6000.0f;

	// ==================== End-Zone Cutoff ====================

	/** When player progress exceeds this fraction (0..1), stop spawning so the button area stays clear */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Spawn", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxProgressToSpawn = 0.95f;

	// ==================== API ====================

	/** 0.0 at the start of the bridge, 1.0 at the far end. Computed from player projection onto the spline. */
	UFUNCTION(BlueprintPure, Category = "Bridge")
	float GetPlayerProgress01() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	virtual int32 GetEffectiveMaxSustainEnemies() const override;
	virtual bool IsSpawnPointAvailable(AArenaSpawnPoint* Point) const override;

private:
	/** Resolved spline component from BridgeSplineActor */
	UPROPERTY()
	TObjectPtr<USplineComponent> CachedSpline;

	/** Time accumulator for the next periodic spawn attempt */
	float TimeSinceLastBridgeSpawn = 0.0f;

	/** Current spawn interval given player progress (lerp Start→End) */
	float ComputeCurrentSpawnInterval() const;

	/** Project a world location to a normalized distance along the spline (0..1). */
	float ProjectToBridge01(const FVector& WorldLocation) const;

	/** Cached player progress for the current frame — avoid re-projecting per spawn-point check */
	mutable float CachedPlayerProgress01 = 0.0f;

	/** Cached player distance along spline (cm) — avoids re-projecting per check */
	mutable float CachedPlayerDistanceAlongSpline = 0.0f;

	/** Recompute player projection caches once per spawn-decision cycle */
	void RefreshPlayerProjectionCache() const;

	/** Total spline length (cm) — cached at BeginPlay */
	float CachedSplineLength = 0.0f;
};
