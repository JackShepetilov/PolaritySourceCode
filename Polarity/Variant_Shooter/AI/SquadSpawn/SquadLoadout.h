// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SquadLoadout.generated.h"

class APawn;

/** What a freshly spawned squad does. v1 has exactly two tasks - enough to run an
 *  attack-on-defence scenario between two factions. [решено 25.08] */
UENUM(BlueprintType)
enum class ESquadInitialTask : uint8
{
	/** Hold near the spawn point, fight whoever comes into perception */
	Defend UMETA(DisplayName = "Defend"),

	/** Advance toward the nearest hostile squad position until enemies are engaged */
	Attack UMETA(DisplayName = "Attack")
};

/** Shape a squad holds while it marches.
 *
 *  Only the march: once the shooting starts every member fights from its own behaviour tree, and a
 *  formation held under fire is a firing squad. Deliberately an enum rather than a bool so the list
 *  can grow (echelon, staggered column, and terrain-driven shapes) without touching call sites. */
UENUM(BlueprintType)
enum class ESquadFormation : uint8
{
	/** Slowest member at the point, the rest fanned out behind on both sides. The default: it keeps
	 *  everyone's fire forward and gives the escort a reason to be beside the tank rather than in
	 *  front of it. */
	Wedge UMETA(DisplayName = "Wedge"),

	/** Shoulder to shoulder, facing the advance. Wide frontage, nothing covering the flanks. */
	Line UMETA(DisplayName = "Line"),

	/** Single file. For corridors, and for arriving somewhere in an order you chose. */
	Column UMETA(DisplayName = "Column")
};

/** One row of the loadout: which NPC class and how many */
USTRUCT(BlueprintType)
struct POLARITY_API FSquadLoadoutEntry
{
	GENERATED_BODY()

	/** Pawn class to spawn (NPC characters, flying drones, the tracked tank - anything possessed
	 *  by its default controller works) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry")
	TSubclassOf<APawn> NPCClass = nullptr;

	/** How many of this class */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (ClampMin = "1", ClampMax = "15"))
	int32 Count = 1;

	/** The squad's commander comes from this row. Whether it lives decides how the squad breaks: an
	 *  orderly withdrawal with covering fire, or a rout with the weapons down.
	 *
	 *  Nothing ticked means the first member spawned takes it, which is what happened implicitly
	 *  before. Several ticked means the first of them; the point is that the choice is visible in the
	 *  asset instead of hiding in the order of the rows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry")
	bool bProvidesCommander = false;
};

/**
 * WHAT to spawn as one squad: a composition of NPC classes, a faction team id and the initial
 * task. Pure data - survives the arrival of real ASquad entities (architecture step 4) unchanged.
 *
 * Team ids [решено 25.08]: 0 = players, 1 = faction A (humans), 2 = faction B (robots).
 */
UCLASS(BlueprintType)
class POLARITY_API USquadLoadout : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	/** Squad composition */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad")
	TArray<FSquadLoadoutEntry> Members;

	/** Which side this squad fights for */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad", meta = (ClampMin = "0", ClampMax = "3"))
	uint8 FactionTeamId = 1;

	/** What the squad does right after spawning */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad")
	ESquadInitialTask InitialTask = ESquadInitialTask::Defend;

	/** Shape held while advancing */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad|March")
	ESquadFormation Formation = ESquadFormation::Wedge;

	/** Distance between neighbouring slots (cm). Wide enough that one blast does not catch two. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad|March", meta = (ClampMin = "150", ClampMax = "3000"))
	float FormationSpacing = 500.0f;

	// ==================== Nerve ====================

	/** Fraction of the squad still standing at which it breaks off and pulls back to where it came
	 *  from. 0.34 means "two of three down". Zero = fights to the last, which is what everything in
	 *  this project did before and is why every engagement ended in mutual annihilation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad|Nerve", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WithdrawStrength = 0.34f;

	/** Seconds of holding the rally point, without losing anybody else, before it goes back in */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad|Nerve", meta = (ClampMin = "0.0", ClampMax = "120.0"))
	float RegroupSeconds = 20.0f;
};
