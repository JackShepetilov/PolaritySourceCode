// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProjectilePoolSubsystem.h"
#include "ShooterProjectile.h"
#include "Engine/World.h"

bool UProjectilePoolSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Create for all game worlds, skip editor preview worlds
	if (UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UProjectilePoolSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UProjectilePoolSubsystem::Deinitialize()
{
	// Clear all pools - projectiles will be destroyed with the world
	PoolsByClass.Empty();
	ActiveCountByClass.Empty();

	Super::Deinitialize();
}

AShooterProjectile* UProjectilePoolSubsystem::GetProjectile(
	TSubclassOf<AShooterProjectile> ProjectileClass,
	const FTransform& SpawnTransform,
	AActor* Owner,
	APawn* Instigator)
{
	if (!ProjectileClass)
	{
		return nullptr;
	}

	AShooterProjectile* Projectile = nullptr;

	// Try to get from pool
	TArray<AShooterProjectile*>* Pool = PoolsByClass.Find(ProjectileClass);
	if (Pool && Pool->Num() > 0)
	{
		Projectile = Pool->Pop();
	}
	else
	{
		// Pool empty or doesn't exist - spawn new projectile
		Projectile = SpawnPooledProjectile(ProjectileClass);

		// If this is first spawn for this class, prewarm with default pool size
		if (!Pool)
		{
			// Get default pool size from CDO
			if (const AShooterProjectile* CDO = ProjectileClass.GetDefaultObject())
			{
				int32 DefaultSize = CDO->GetDefaultPoolSize();
				if (DefaultSize > 1) // Already spawned one
				{
					PrewarmPool(ProjectileClass, DefaultSize - 1);
				}
			}
		}
	}

	if (Projectile)
	{
		// Activate the projectile
		Projectile->ActivateFromPool(SpawnTransform, Owner, Instigator);

		// Track active count
		int32& ActiveCount = ActiveCountByClass.FindOrAdd(ProjectileClass);
		ActiveCount++;
	}

	return Projectile;
}

void UProjectilePoolSubsystem::ReturnProjectile(AShooterProjectile* Projectile)
{
	if (!Projectile)
	{
		return;
	}

	TSubclassOf<AShooterProjectile> ProjectileClass = Projectile->GetClass();

	// Deactivate the projectile
	Projectile->DeactivateToPool();

	// Return to pool
	TArray<AShooterProjectile*>& Pool = PoolsByClass.FindOrAdd(ProjectileClass);
	Pool.Add(Projectile);

	// Update active count
	if (int32* ActiveCount = ActiveCountByClass.Find(ProjectileClass))
	{
		(*ActiveCount)--;
	}
}

void UProjectilePoolSubsystem::PrewarmPool(TSubclassOf<AShooterProjectile> ProjectileClass, int32 Count)
{
	if (!ProjectileClass || Count <= 0)
	{
		return;
	}

	TArray<AShooterProjectile*>& Pool = PoolsByClass.FindOrAdd(ProjectileClass);
	Pool.Reserve(Pool.Num() + Count);

	for (int32 i = 0; i < Count; ++i)
	{
		if (AShooterProjectile* Projectile = SpawnPooledProjectile(ProjectileClass))
		{
			Pool.Add(Projectile);
		}
	}
}

int32 UProjectilePoolSubsystem::GetPoolSize(TSubclassOf<AShooterProjectile> ProjectileClass) const
{
	if (const TArray<AShooterProjectile*>* Pool = PoolsByClass.Find(ProjectileClass))
	{
		return Pool->Num();
	}
	return 0;
}

int32 UProjectilePoolSubsystem::GetActiveCount(TSubclassOf<AShooterProjectile> ProjectileClass) const
{
	if (const int32* Count = ActiveCountByClass.Find(ProjectileClass))
	{
		return *Count;
	}
	return 0;
}

AShooterProjectile* UProjectilePoolSubsystem::SpawnPooledProjectile(TSubclassOf<AShooterProjectile> ProjectileClass)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// Use deferred spawn to set bIsPooled BEFORE BeginPlay
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bDeferConstruction = true;

	AShooterProjectile* Projectile = World->SpawnActor<AShooterProjectile>(
		ProjectileClass,
		FTransform::Identity,
		SpawnParams
	);

	if (Projectile)
	{
		// Mark as pooled BEFORE BeginPlay runs
		Projectile->SetPooledFlag();

		// Now finish spawning (this calls BeginPlay)
		Projectile->FinishSpawning(FTransform::Identity);

		// Deactivate for pool storage
		Projectile->DeactivateToPool();
	}

	return Projectile;
}
