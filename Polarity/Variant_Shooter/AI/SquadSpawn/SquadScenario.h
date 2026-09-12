// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SquadScenario.generated.h"

class USquadLoadout;

/** One scenario row: which loadout to spawn at which tagged spawn point */
USTRUCT(BlueprintType)
struct POLARITY_API FSquadScenarioEntry
{
	GENERATED_BODY()

	/** Tag of the ASquadSpawnPoint actors this entry spawns at (all matching points) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry")
	FName PointTag = NAME_None;

	/** What to spawn there */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry")
	TObjectPtr<USquadLoadout> Loadout = nullptr;

	/** Which tagged point this squad is sent at, when it is an Attack squad. Empty means "whatever
	 *  enemy position is nearest", which is fine for two squads facing each other and wrong the
	 *  moment there are three points: a squad in the middle would pick its objective by geometry and
	 *  quietly march at the wrong one. A live enemy still overrides this - you fight what you can
	 *  see, and the objective is only where you go when you can see nobody. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry")
	FName TargetPointTag = NAME_None;
};

/**
 * HOW to assemble a test: pairs of "spawn point tag -> loadout". One playground level carries
 * many tagged points; scenarios pick subsets, so no duplicate levels per composition.
 */
UCLASS(BlueprintType)
class POLARITY_API USquadScenario : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	/** Which squads appear where */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	TArray<FSquadScenarioEntry> Entries;

	/** Free-form description of what this run tests (for notes now, BattleLog later) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	FText Description;
};
