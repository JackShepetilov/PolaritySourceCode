// DroneTestSpawner.h
// Debug-room helper: keeps exactly one NPC (a drone) alive. Spawns it on BeginPlay and, the moment it
// dies, spawns the next one at the same spot. For tuning flight and parry, not for gameplay.

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

	TWeakObjectPtr<AShooterNPC> CurrentDrone;
	FTimerHandle RespawnTimer;
	int32 SpawnCount = 0;
};
