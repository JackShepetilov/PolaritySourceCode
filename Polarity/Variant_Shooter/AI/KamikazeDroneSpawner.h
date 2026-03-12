// KamikazeDroneSpawner.h
// Spawner for KamikazeDrones with two modes:
// Normal — protected by shield guardians (Dummies), moderate spawn rate.
// Panic  — guardians dead, shield down, high spawn rate, spawner is damageable.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KamikazeDroneSpawner.generated.h"

class AKamikazeDroneNPC;
class USphereComponent;
class UNiagaraSystem;
class UNiagaraComponent;

UENUM(BlueprintType)
enum class ESpawnerMode : uint8
{
	Normal,
	Panic
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSpawnerDestroyed, AKamikazeDroneSpawner*, Spawner);

UCLASS()
class POLARITY_API AKamikazeDroneSpawner : public AActor
{
	GENERATED_BODY()

public:
	AKamikazeDroneSpawner();

	// ── Delegates ──────────────────────────────────────────────
	UPROPERTY(BlueprintAssignable, Category = "Spawner|Events")
	FOnSpawnerDestroyed OnSpawnerDestroyed;

protected:
	virtual void BeginPlay() override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	// ── Components ─────────────────────────────────────────────
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawner")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawner")
	TObjectPtr<USphereComponent> DetectionSphere;

	// ── State ──────────────────────────────────────────────────
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawner|State")
	ESpawnerMode CurrentMode = ESpawnerMode::Normal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawner|State")
	bool bIsActivated = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawner|State")
	bool bIsShieldActive = true;

	// ── Health ─────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Health")
	float MaxHP = 500.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawner|Health")
	float CurrentHP;

	// ── Shield Guardians ───────────────────────────────────────
	/** Level-placed actors that protect this spawner. Kill them all to break the shield. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Spawner|Shield")
	TArray<TSoftObjectPtr<AActor>> ShieldGuardians;

	// ── Spawn Settings — Normal ────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Normal")
	float SpawnInterval_Normal = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Normal")
	int32 MaxDrones_Normal = 3;

	// ── Spawn Settings — Panic ─────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Panic")
	float SpawnInterval_Panic = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Panic")
	int32 MaxDrones_Panic = 6;

	// ── Spawn Geometry ─────────────────────────────────────────
	/** Horizontal radius around spawner where drones materialize. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Spawn")
	float SpawnRadius = 300.f;

	/** Height offset above spawner for spawn points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Spawn")
	float SpawnHeight = 200.f;

	// ── Launch Settings ────────────────────────────────────────
	/** Initial launch speed when drone is ejected from spawner (cm/s).
	 *  Drone's orbit system will naturally decelerate it to CruiseSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Launch", meta = (ClampMin = "0"))
	float LaunchSpeed = 1500.f;

	/** Launch direction in spawner's local space (normalized automatically).
	 *  X = forward, Y = right, Z = up. Default = forward + slightly up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Launch")
	FVector LaunchDirection = FVector(1.f, 0.f, 0.3f);

	/** Random spread cone half-angle (degrees). 0 = perfectly straight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Launch", meta = (ClampMin = "0", ClampMax = "90"))
	float LaunchSpreadAngle = 15.f;

	// ── Player Detection ───────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Detection")
	float ReactionRadius = 3000.f;

	// ── Drone Config ───────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Drone")
	TSubclassOf<AKamikazeDroneNPC> DroneClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawner|Drone")
	TArray<TObjectPtr<AKamikazeDroneNPC>> ActiveDrones;

	// ── VFX ────────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|VFX")
	TObjectPtr<UNiagaraSystem> SpawnVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|VFX")
	TObjectPtr<UNiagaraSystem> ShieldVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|VFX")
	TObjectPtr<UNiagaraSystem> DeathVFX;

	// ── SFX ────────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|SFX")
	TObjectPtr<USoundBase> SpawnSFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|SFX")
	TObjectPtr<USoundBase> ShieldBreakSFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|SFX")
	TObjectPtr<USoundBase> DeathSFX;

	// ── Active shield VFX component (spawned at runtime) ──────
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ShieldVFXComponent;

private:
	// ── Internal ───────────────────────────────────────────────
	FTimerHandle SpawnTimerHandle;

	/** Resolved guardian pointers for fast access. */
	TArray<TWeakObjectPtr<AActor>> ResolvedGuardians;

	int32 GuardiansAlive = 0;

	UFUNCTION()
	void OnDetectionBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	void Activate();
	void SpawnDrone();
	void StartSpawnTimer();

	UFUNCTION()
	void OnGuardianDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void OnDroneDestroyed(AActor* DestroyedActor);

	void EnterPanicMode();
	void Die();

	int32 GetMaxDronesForCurrentMode() const;
	float GetSpawnIntervalForCurrentMode() const;
};
