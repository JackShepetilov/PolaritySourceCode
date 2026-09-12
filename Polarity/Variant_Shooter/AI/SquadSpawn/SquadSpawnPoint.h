// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SquadSpawnPoint.generated.h"

class USphereComponent;
class USquadLoadout;
class USquadSpawnSubsystem;

/**
 * WHERE a squad spawns: a tagged point on the map with an optional default loadout.
 * One playground level carries many of these; scenarios address them by PointTag.
 */
UCLASS()
class POLARITY_API ASquadSpawnPoint : public AActor
{
	GENERATED_BODY()

public:

	ASquadSpawnPoint();

	// ==================== Configuration ====================

	/** Scenarios and console commands address this point by tag */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad Spawn")
	FName PointTag = NAME_None;

	/** Loadout used by SpawnOnBeginPlay / when a command names no loadout */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad Spawn")
	TObjectPtr<USquadLoadout> DefaultLoadout = nullptr;

	/** Spawn the DefaultLoadout here as soon as play begins (static scenes without console) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad Spawn")
	bool bSpawnOnBeginPlay = false;

	/** Members are scattered within this radius around the point (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad Spawn", meta = (ClampMin = "50", ClampMax = "4000"))
	float SpawnRadius = 400.0f;

	// ==================== API ====================

	/** Spawn this point's squad. Pass null to use DefaultLoadout. Returns members spawned. */
	/** Spawn the squad. Objective is where an Attack squad is sent when it can see nobody; pass
	 *  nullptr to leave it at "nearest enemy position". */
	int32 SpawnSquad(USquadLoadout* OverrideLoadout, const FVector* Objective = nullptr);

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Editor gizmo: wireframe sphere showing the scatter radius */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USphereComponent> RadiusGizmo;
};
