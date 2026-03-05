// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RewardContainer.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class UGeometryCollection;
class AGeometryCollectionActor;
class UNiagaraSystem;

/**
 * Floating reward container that drops and shatters on impact.
 *
 * Place suspended in the air. When Activate() is called (by ArenaManager after
 * the reward dummy dies), the static mesh swaps to a Geometry Collection that
 * falls under gravity. On ground impact the GC shatters and a reward actor spawns.
 */
UCLASS(Blueprintable)
class POLARITY_API ARewardContainer : public AActor
{
	GENERATED_BODY()

public:
	ARewardContainer();

	// ==================== Components ====================

	/** Visual static mesh — visible in editor and at runtime until activation */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ContainerMesh;

	/** Invisible collision sphere that falls alongside GC to detect ground impact */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> ImpactProbe;

	// ==================== Geometry Collection ====================

	/** GC asset matching the ContainerMesh. Spawned whole on activation, shattered on impact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Destruction")
	TObjectPtr<UGeometryCollection> ContainerGC;

	/** Collision profile for GC gibs after shattering */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Destruction")
	FName GibCollisionProfile = FName("Ragdoll");

	/** Radial impulse applied to gibs on break (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Destruction", meta = (ClampMin = "0.0"))
	float BreakImpulse = 400.0f;

	/** Angular impulse for tumbling gibs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Destruction", meta = (ClampMin = "0.0"))
	float BreakAngularImpulse = 50.0f;

	/** How long gib fragments persist (seconds). 0 = never destroy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Destruction", meta = (ClampMin = "0.0"))
	float GibLifetime = 30.0f;

	/** Seconds after breaking to freeze gibs (disable physics). 0 = never freeze. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Destruction", meta = (ClampMin = "0.0"))
	float GibFreezeTime = 3.0f;

	// ==================== Reward Spawning ====================

	/** Actor class to spawn at impact location (e.g. UpgradePickup) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Spawn")
	TSubclassOf<AActor> RewardActorClass;

	/** Vertical offset from impact point for reward spawn (so it doesn't clip floor) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Spawn", meta = (Units = "cm"))
	float RewardSpawnZOffset = 50.0f;

	// ==================== VFX / SFX ====================

	/** VFX spawned at impact (dust cloud, sparks) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Effects")
	TObjectPtr<UNiagaraSystem> ImpactVFX;

	/** Scale of impact VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Effects", meta = (ClampMin = "0.1"))
	float ImpactVFXScale = 1.0f;

	/** Sound played on ground impact */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Effects")
	TObjectPtr<USoundBase> ImpactSound;

	// ==================== Impact Probe ====================

	/** Radius of the invisible impact probe sphere (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Impact Probe", meta = (ClampMin = "5.0", Units = "cm"))
	float ImpactProbeRadius = 30.0f;

	// ==================== API ====================

	/** Start the drop sequence. Called by ArenaManager when the reward dummy dies. */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void Activate();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Prevents double activation */
	bool bActivated = false;

	/** Has the GC hit the ground? */
	bool bImpacted = false;

	/** Spawned GC actor (kept alive until gibs expire) */
	UPROPERTY()
	TObjectPtr<AGeometryCollectionActor> SpawnedGCActor;

	/** World location where impact occurred */
	FVector ImpactLocation;

	/** Spawn GC actor at ContainerMesh transform, whole and unbroken, with physics + gravity */
	void SpawnFallingGC();

	/** Called when ImpactProbe hits WorldStatic */
	UFUNCTION()
	void OnImpactProbeHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/** Shatter the GC, spawn VFX/SFX, schedule gib freeze, spawn reward */
	void BreakContainer();

	/** Spawn the reward actor at ImpactLocation */
	void SpawnReward();

	FTimerHandle GibFreezeHandle;
};
