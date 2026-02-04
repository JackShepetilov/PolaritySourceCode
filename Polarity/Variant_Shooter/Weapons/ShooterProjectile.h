// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShooterProjectile.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class ACharacter;
class UPrimitiveComponent;
class UNiagaraSystem;
class UNiagaraComponent;

/**
 *  Simple projectile class for a first person shooter game
 */
UCLASS(abstract)
class POLARITY_API AShooterProjectile : public AActor
{
	GENERATED_BODY()

protected:

	/** Provides collision detection for the projectile */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	USphereComponent* CollisionComponent;

	/** Handles movement for the projectile */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	UProjectileMovementComponent* ProjectileMovement;

	/** Loudness of the AI perception noise done by this projectile on hit */
	UPROPERTY(EditAnywhere, Category="Projectile|Noise", meta = (ClampMin = 0, ClampMax = 100))
	float NoiseLoudness = 3.0f;

	/** Range of the AI perception noise done by this projectile on hit */
	UPROPERTY(EditAnywhere, Category="Projectile|Noise", meta = (ClampMin = 0, ClampMax = 100000, Units = "cm"))
	float NoiseRange = 3000.0f;

	/** Tag of the AI perception noise done by this projectile on hit */
	UPROPERTY(EditAnywhere, Category="Noise")
	FName NoiseTag = FName("Projectile");

	/** Physics force to apply on hit */
	UPROPERTY(EditAnywhere, Category="Projectile|Hit", meta = (ClampMin = 0, ClampMax = 50000))
	float PhysicsForce = 100.0f;

	/** Damage to apply on hit */
	UPROPERTY(EditAnywhere, Category="Projectile|Hit", meta = (ClampMin = 0, ClampMax = 100))
	float HitDamage = 25.0f;

	/** Type of damage to apply. Can be used to represent specific types of damage such as fire, explosion, etc. */
	UPROPERTY(EditAnywhere, Category="Projectile|Hit")
	TSubclassOf<UDamageType> HitDamageType;

	/** If true, the projectile can damage the character that shot it */
	UPROPERTY(EditAnywhere, Category="Projectile|Hit")
	bool bDamageOwner = false;

	/** Damage multipliers based on target actor tags. Multiple matching tags multiply together. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projectile|Hit")
	TMap<FName, float> TagDamageMultipliers;

	/** If true, the projectile will explode and apply radial damage to all actors in range */
	UPROPERTY(EditAnywhere, Category="Projectile|Explosion")
	bool bExplodeOnHit = false;

	/** Max distance for actors to be affected by explosion damage */
	UPROPERTY(EditAnywhere, Category="Projectile|Explosion", meta = (ClampMin = 0, ClampMax = 5000, Units = "cm"))
	float ExplosionRadius = 500.0f;	

	/** If true, this projectile has already hit another surface */
	bool bHit = false;

	/** How long to wait after a hit before destroying this projectile */
	UPROPERTY(EditAnywhere, Category="Projectile|Destruction", meta = (ClampMin = 0, ClampMax = 10, Units = "s"))
	float DeferredDestructionTime = 5.0f;

	/** Timer to handle deferred destruction of this projectile */
	FTimerHandle DestructionTimer;

	// ==================== Pooling ====================

	/** Default number of projectiles to prewarm in pool */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Pooling", meta = (ClampMin = "1", ClampMax = "200"))
	int32 DefaultPoolSize = 20;

	/** True if this projectile is managed by the pool system */
	bool bIsPooled = false;

	// ==================== VFX|Trail ====================

	/** Niagara system for projectile trail effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|VFX")
	TObjectPtr<UNiagaraSystem> TrailFX;

	/** Active trail component (spawned on BeginPlay) */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> TrailComponent;

public:

	/** Constructor */
	AShooterProjectile();

	// ==================== Pooling Interface ====================

	/** Get default pool size for this projectile class */
	int32 GetDefaultPoolSize() const { return DefaultPoolSize; }

	/** Set pooled flag before BeginPlay (called by pool subsystem during deferred spawn) */
	void SetPooledFlag() { bIsPooled = true; }

	/** Called by pool to activate projectile for use */
	void ActivateFromPool(const FTransform& SpawnTransform, AActor* NewOwner, APawn* NewInstigator);

	/** Called by pool to deactivate projectile for reuse */
	void DeactivateToPool();

protected:
	
	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Gameplay cleanup */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	/** Handles collision */
	virtual void NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

protected:

	/** Looks up actors within the explosion radius and damages them */
	void ExplosionCheck(const FVector& ExplosionCenter);

	/** Processes a projectile hit for the given actor */
	virtual void ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& HitLocation, const FVector& HitDirection);

	/** Calculate damage multiplier based on target's tags */
	float GetTagDamageMultiplier(AActor* Target) const;

	/** Passes control to Blueprint to implement any effects on hit. */
	UFUNCTION(BlueprintImplementableEvent, Category="Projectile", meta = (DisplayName = "On Projectile Hit"))
	void BP_OnProjectileHit(const FHitResult& Hit);

	/** Called from the destruction timer to destroy this projectile */
	void OnDeferredDestruction();

	/** Reset projectile state for pool reuse. Override in subclasses for custom state. */
	virtual void ResetProjectileState();

	/** Return this projectile to pool (or destroy if not pooled) */
	void ReturnToPoolOrDestroy();
};
