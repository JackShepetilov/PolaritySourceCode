// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArenaWaveData.generated.h"

class AShooterNPC;

/**
 * Current state of a combat arena.
 */
UENUM(BlueprintType)
enum class EArenaState : uint8
{
	/** Waiting for player to enter */
	Idle,
	/** Combat in progress */
	Active,
	/** Pause between waves */
	BetweenWaves,
	/** All waves cleared */
	Completed
};

/**
 * Single entry in a wave: NPC class + count.
 */
USTRUCT(BlueprintType)
struct FArenaSpawnEntry
{
	GENERATED_BODY()

	/** Class of NPC to spawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	TSubclassOf<AShooterNPC> NPCClass;

	/** How many of this NPC to spawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena", meta = (ClampMin = "1"))
	int32 Count = 1;
};

/**
 * A single wave of enemies.
 */
USTRUCT(BlueprintType)
struct FArenaWave
{
	GENERATED_BODY()

	/** NPCs to spawn in this wave */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	TArray<FArenaSpawnEntry> Entries;

	/** Additional delay before this specific wave starts (added to TimeBetweenWaves) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena", meta = (ClampMin = "0.0"))
	float DelayBeforeWave = 0.0f;
};
