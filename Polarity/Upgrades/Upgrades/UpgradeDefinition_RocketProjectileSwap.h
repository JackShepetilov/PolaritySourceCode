// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "Variant_Shooter/Weapons/ShooterProjectile.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "UpgradeDefinition_RocketProjectileSwap.generated.h"

USTRUCT(BlueprintType)
struct FRocketProjectileSwapLevelData
{
	GENERATED_BODY()

	/** Specific weapon class this upgrade applies to. Set this to the rocket launcher Blueprint class. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rocket Projectile Swap")
	TSubclassOf<AShooterWeapon> RequiredWeaponClass;

	/** Projectile class fired by the eligible weapon while this upgrade is active. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rocket Projectile Swap")
	TSubclassOf<AShooterProjectile> UpgradedProjectileClass;
};

UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_RocketProjectileSwap : public UUpgradeDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rocket Projectile Swap")
	TArray<FRocketProjectileSwapLevelData> LevelData;

	UFUNCTION(BlueprintPure, Category = "Rocket Projectile Swap")
	const FRocketProjectileSwapLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
