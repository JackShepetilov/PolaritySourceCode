// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArenaWaveData.generated.h"

class AShooterNPC;

/**
 * Arena operating mode.
 */
UENUM(BlueprintType)
enum class EArenaMode : uint8
{
	/** Classic wave-based spawning */
	Waves,
	/** Continuous spawning: kill one enemy, another spawns */
	Sustain
};

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
 * Weighted enemy entry for sustain mode.
 */
USTRUCT(BlueprintType)
struct FSustainSpawnEntry
{
	GENERATED_BODY()

	/** Class of NPC to spawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	TSubclassOf<AShooterNPC> NPCClass;

	/** Relative spawn weight (higher = more likely). Not a percentage — just relative to other entries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena", meta = (ClampMin = "0.01"))
	float Weight = 1.0f;
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
