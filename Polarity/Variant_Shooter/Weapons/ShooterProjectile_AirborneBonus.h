// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterProjectile.h"
#include "ShooterProjectile_AirborneBonus.generated.h"

/**
 * Projectile variant for upgrades that reward juggling airborne enemies.
 * Speed, knockback, radius and base damage are still configured on the projectile defaults.
 */
UCLASS(Blueprintable)
class POLARITY_API AShooterProjectile_AirborneBonus : public AShooterProjectile
{
	GENERATED_BODY()

public:
	AShooterProjectile_AirborneBonus();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Airborne Bonus", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float AirborneDamageMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Airborne Bonus")
	bool bTreatFlyingMovementModeAsAirborne = true;

	virtual float GetProjectileDamageMultiplier(AActor* Target) const override;

private:
	bool IsAirborneTarget(const AActor* Target) const;
};
