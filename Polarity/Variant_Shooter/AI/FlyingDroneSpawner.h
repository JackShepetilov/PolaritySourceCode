// FlyingDroneSpawner.h
// Button-triggered spawner for FlyingDrones.
// Spawns one drone at a time. When the drone dies, broadcasts OnDroneKilled.
// Button is added in Blueprint via ShootableButtonComponent — not hardcoded here.
// Designed for side-quest loot farming: drones may drop quest items on death.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlyingDroneSpawner.generated.h"

class AFlyingDrone;
class UNiagaraSystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDroneSpawned, AFlyingDrone*, Drone);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDroneKilled);

UCLASS()
class POLARITY_API AFlyingDroneSpawner : public AActor
{
	GENERATED_BODY()

public:
	AFlyingDroneSpawner();

	// ── Delegates ──────────────────────────────────────────────

	/** Fired when a drone is spawned. */
	UPROPERTY(BlueprintAssignable, Category = "DroneSpawner|Events")
	FOnDroneSpawned OnDroneSpawned;

	/** Fired when the active drone is killed. */
	UPROPERTY(BlueprintAssignable, Category = "DroneSpawner|Events")
	FOnDroneKilled OnDroneKilled;

	// ── Public Interface ───────────────────────────────────────

	/** Request a drone spawn. Returns true if successful. */
	UFUNCTION(BlueprintCallable, Category = "DroneSpawner")
	bool RequestSpawn();

	/** Returns true if a drone is currently alive. */
	UFUNCTION(BlueprintPure, Category = "DroneSpawner")
	bool IsDroneActive() const { return ActiveDrone != nullptr; }

protected:
	virtual void BeginPlay() override;
	// ── Components ─────────────────────────────────────────────

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DroneSpawner")
	TObjectPtr<USceneComponent> SceneRoot;

	// ── Drone Config ───────────────────────────────────────────

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DroneSpawner|Drone")
	TSubclassOf<AFlyingDrone> DroneClass;

	// ── Spawn Geometry ─────────────────────────────────────────

	/** Height offset above spawner for spawn point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DroneSpawner|Spawn")
	float SpawnHeight = 200.f;

	// ── Launch Settings ────────────────────────────────────────

	/** Initial launch speed (cm/s). 0 = drone spawns stationary. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DroneSpawner|Launch", meta = (ClampMin = "0"))
	float LaunchSpeed = 800.f;

	/** Launch direction in spawner's local space (normalized automatically). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DroneSpawner|Launch")
	FVector LaunchDirection = FVector(0.f, 0.f, 1.f);

	/** Random spread cone half-angle (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DroneSpawner|Launch", meta = (ClampMin = "0", ClampMax = "90"))
	float LaunchSpreadAngle = 10.f;

	// ── VFX / SFX ──────────────────────────────────────────────

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DroneSpawner|VFX")
	TObjectPtr<UNiagaraSystem> SpawnVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DroneSpawner|SFX")
	TObjectPtr<USoundBase> SpawnSFX;

	// ── State ──────────────────────────────────────────────────

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DroneSpawner|State")
	TObjectPtr<AFlyingDrone> ActiveDrone;

private:
	void SpawnDrone();

	UFUNCTION()
	void OnActiveDroneDestroyed(AActor* DestroyedActor);
};
