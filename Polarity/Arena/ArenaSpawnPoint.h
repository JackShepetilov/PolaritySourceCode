// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaSpawnPoint.generated.h"

class AShooterNPC;

/**
 * Marker actor placed in arena levels to designate NPC spawn locations.
 * No gameplay logic — just a transform + spawn type for ArenaManager to use.
 */
UCLASS(Blueprintable)
class POLARITY_API AArenaSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AArenaSpawnPoint();

	/** Whether this point spawns airborne NPCs (drones) at AirSpawnHeight above the point */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn Point")
	bool bAirSpawn = false;

	/** Height offset for air spawns (cm above the spawn point) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn Point", meta = (ClampMin = "100.0", EditCondition = "bAirSpawn"))
	float AirSpawnHeight = 300.0f;

	/** NPC classes that are NOT allowed to spawn at this point.
	 *  Empty = all classes allowed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn Point")
	TArray<TSubclassOf<AShooterNPC>> ExcludedNPCClasses;

	/** Returns true if the given NPC class is allowed to spawn here */
	bool IsClassAllowed(TSubclassOf<AShooterNPC> NPCClass) const;

	/** Get the spawn transform, accounting for air spawn offset */
	UFUNCTION(BlueprintPure, Category = "Spawn Point")
	FTransform GetSpawnTransform(bool bForAirUnit) const;

protected:
#if WITH_EDITORONLY_DATA
	/** Billboard for visibility in editor */
	UPROPERTY()
	TObjectPtr<class UBillboardComponent> EditorSprite;

	/** Arrow showing spawn facing direction */
	UPROPERTY()
	TObjectPtr<class UArrowComponent> EditorArrow;
#endif
};
