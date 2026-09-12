// KeepAliveSpawner.h
// Keeps a number of NPCs of one class alive: spawns them on BeginPlay and replaces each one that dies.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KeepAliveSpawner.generated.h"

class AShooterNPC;
class UArrowComponent;
class AKeepAliveSpawner;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKeepAliveCleared, AKeepAliveSpawner*, Spawner);

/**
 * Holds KeepAlive NPCs of DroneClass in the level (carriers, kamikaze drones, anything that is an
 * AShooterNPC). Spawns them on BeginPlay and, the moment one dies, spawns a replacement after
 * RespawnDelay. Server only: the NPCs replicate.
 *
 * The count is meant to be driven from outside later (the wave number): SetKeepAlive raises it and
 * fills the gap at once; lowering it lets the extra ones live, it just stops replacing them.
 */
UCLASS()
class POLARITY_API AKeepAliveSpawner : public AActor
{
	GENERATED_BODY()

public:

	AKeepAliveSpawner();

	/** What to keep alive. */
	UPROPERTY(EditAnywhere, Category = "Keep Alive")
	TSubclassOf<AShooterNPC> DroneClass;

	/** Pause between a death and its replacement. Zero means the next frame. */
	UPROPERTY(EditAnywhere, Category = "Keep Alive", meta = (ClampMin = "0.0", Units = "s"))
	float RespawnDelay = 0.0f;

	/** How many to keep alive at once. */
	UPROPERTY(EditAnywhere, Category = "Keep Alive", meta = (ClampMin = "0", ClampMax = "20"))
	int32 KeepAlive = 1;

	/** How many it may spawn in total, the first ones included. -1 = no limit. With a limit, the
	 *  spawner stops replacing once it is spent and reports OnCleared when the last one dies. */
	UPROPERTY(EditAnywhere, Category = "Keep Alive", meta = (ClampMin = "-1"))
	int32 TotalToSpawn = -1;

	/** The budget is spent and every NPC it spawned is dead: the wave this spawner runs is over. */
	UPROPERTY(BlueprintAssignable, Category = "Keep Alive")
	FOnKeepAliveCleared OnCleared;

	/** Start over with a new budget, for the next wave. Keeps whatever is still alive. */
	UFUNCTION(BlueprintCallable, Category = "Keep Alive")
	void ResetBudget(int32 NewTotalToSpawn);

	/** Where they appear, in turn. Empty = at this actor, spread a little sideways. Place them where
	 *  the NPC should enter from (for a carrier: in the air at the edge of the fog). */
	UPROPERTY(EditInstanceOnly, Category = "Keep Alive")
	TArray<TObjectPtr<AActor>> SpawnPoints;

	/** Change the count while playing. Raising it spawns the difference now. */
	UFUNCTION(BlueprintCallable, Category = "Keep Alive")
	void SetKeepAlive(int32 NewKeepAlive);

	UFUNCTION(BlueprintPure, Category = "Keep Alive")
	int32 GetAliveCount() const;

	/** Destroy everything it spawned that is still alive, without replacing any of it. For a debug
	 *  mode switch: lower KeepAlive first or the next FillUp brings them back. */
	UFUNCTION(BlueprintCallable, Category = "Keep Alive")
	void DespawnAll();

	/** How many this spawner has made so far. */
	UFUNCTION(BlueprintPure, Category = "Keep Alive")
	int32 GetSpawnCount() const { return SpawnCount; }

protected:

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Keep Alive")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Keep Alive")
	TObjectPtr<UArrowComponent> Arrow;

private:

	void SpawnNext();
	void FillUp();
	void ScheduleNext();
	FTransform NextSpawnTransform();

	UFUNCTION()
	void HandleDeath(AShooterNPC* DeadNPC);

	UFUNCTION()
	void HandleDestroyed(AActor* DestroyedActor);

	TArray<TWeakObjectPtr<AShooterNPC>> Alive;
	int32 SpawnCount = 0;
	int32 NextPointIndex = 0;

	/** Spawned under the current budget (ResetBudget starts it over). */
	int32 BudgetSpent = 0;
	bool bClearedBroadcast = false;

	bool IsBudgetSpent() const { return TotalToSpawn >= 0 && BudgetSpent >= TotalToSpawn; }
	void CheckCleared();
};
