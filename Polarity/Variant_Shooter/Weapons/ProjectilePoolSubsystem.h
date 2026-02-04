// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ProjectilePoolSubsystem.generated.h"

class AShooterProjectile;

/**
 * World Subsystem that manages object pooling for projectiles.
 * Eliminates SpawnActor/Destroy overhead during gameplay.
 */
UCLASS()
class POLARITY_API UProjectilePoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	// USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Get a projectile from the pool, or spawn a new one if pool is empty.
	 * The projectile is automatically activated and ready to use.
	 *
	 * @param ProjectileClass - Class of projectile to get
	 * @param SpawnTransform - World transform for the projectile
	 * @param Owner - Owner actor (usually the weapon)
	 * @param Instigator - Instigator pawn (the shooter)
	 * @return Activated projectile ready for use
	 */
	UFUNCTION(BlueprintCallable, Category = "Projectile Pool")
	AShooterProjectile* GetProjectile(
		TSubclassOf<AShooterProjectile> ProjectileClass,
		const FTransform& SpawnTransform,
		AActor* Owner,
		APawn* Instigator
	);

	/**
	 * Return a projectile to the pool for reuse.
	 * Call this instead of Destroy() when projectile hits something.
	 *
	 * @param Projectile - Projectile to return to pool
	 */
	UFUNCTION(BlueprintCallable, Category = "Projectile Pool")
	void ReturnProjectile(AShooterProjectile* Projectile);

	/**
	 * Pre-spawn projectiles to avoid runtime allocation.
	 * Call this during level load or when you know combat is about to start.
	 *
	 * @param ProjectileClass - Class of projectile to prewarm
	 * @param Count - Number of projectiles to create
	 */
	UFUNCTION(BlueprintCallable, Category = "Projectile Pool")
	void PrewarmPool(TSubclassOf<AShooterProjectile> ProjectileClass, int32 Count);

	/** Get current pool size for a projectile class */
	UFUNCTION(BlueprintPure, Category = "Projectile Pool")
	int32 GetPoolSize(TSubclassOf<AShooterProjectile> ProjectileClass) const;

	/** Get number of active (in-use) projectiles for a class */
	UFUNCTION(BlueprintPure, Category = "Projectile Pool")
	int32 GetActiveCount(TSubclassOf<AShooterProjectile> ProjectileClass) const;

protected:

	/** Pool storage: Class -> Array of inactive projectiles */
	TMap<TSubclassOf<AShooterProjectile>, TArray<AShooterProjectile*>> PoolsByClass;

	/** Track active projectiles for statistics */
	TMap<TSubclassOf<AShooterProjectile>, int32> ActiveCountByClass;

	/** Spawn a new projectile for the pool (deactivated) */
	AShooterProjectile* SpawnPooledProjectile(TSubclassOf<AShooterProjectile> ProjectileClass);
};
