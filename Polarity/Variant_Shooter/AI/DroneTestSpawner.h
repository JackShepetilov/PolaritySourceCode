// DroneTestSpawner.h
// Debug-room helper: keeps KeepAlive NPCs (drones) alive, one by default. Spawns them on BeginPlay and,
// the moment one dies, spawns the next at the same spot. For tuning flight and parry, not for gameplay.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DroneTestSpawner.generated.h"

class AShooterNPC;
class UArrowComponent;

UCLASS()
class POLARITY_API ADroneTestSpawner : public AActor
{
	GENERATED_BODY()

public:

	ADroneTestSpawner();

	/** What to keep alive. Spawned at this actor's location, facing along the arrow. */
	UPROPERTY(EditAnywhere, Category = "Drone Test")
	TSubclassOf<AShooterNPC> DroneClass;

	/** Pause between a death and the next spawn. Zero means the next frame. */
	UPROPERTY(EditAnywhere, Category = "Drone Test", meta = (ClampMin = "0.0", Units = "s"))
	float RespawnDelay = 0.0f;

	/** How many drones to keep alive at once. One by default: a single drone at a time for
	 *  measuring; more to test how a group shares out and queues its strikes. */
	UPROPERTY(EditAnywhere, Category = "Drone Test", meta = (ClampMin = "1", ClampMax = "20"))
	int32 KeepAlive = 1;

	/** How many drones this spawner has made so far, the current one included. */
	UFUNCTION(BlueprintPure, Category = "Drone Test")
	int32 GetSpawnCount() const { return SpawnCount; }

protected:

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Drone Test")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Drone Test")
	TObjectPtr<UArrowComponent> Arrow;

private:

	void SpawnNext();
	void ScheduleNext();

	UFUNCTION()
	void HandleDroneDeath(AShooterNPC* DeadNPC);

	UFUNCTION()
	void HandleDroneDestroyed(AActor* DestroyedActor);

	TArray<TWeakObjectPtr<AShooterNPC>> AliveDrones;
	int32 SpawnCount = 0;
};
