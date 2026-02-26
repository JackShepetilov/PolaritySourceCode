// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DestroyedIslandsSubsystem.generated.h"

/**
 * Tracks which destructible islands have been destroyed during this game session.
 * Islands check this subsystem in BeginPlay to stay destroyed when sublevels reload.
 */
UCLASS()
class POLARITY_API UDestroyedIslandsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Mark an island as destroyed */
	UFUNCTION(BlueprintCallable, Category = "Islands")
	void RegisterDestroyedIsland(FName IslandID);

	/** Check if an island was already destroyed this session */
	UFUNCTION(BlueprintPure, Category = "Islands")
	bool IsIslandDestroyed(FName IslandID) const;

	/** Clear all destroyed island records (e.g. on new game) */
	UFUNCTION(BlueprintCallable, Category = "Islands")
	void ClearDestroyedIslands();

private:
	/** Set of destroyed island IDs */
	TSet<FName> DestroyedIslandIDs;
};
