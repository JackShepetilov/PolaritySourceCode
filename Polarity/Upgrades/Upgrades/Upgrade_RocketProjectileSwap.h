// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Variant_Shooter/Weapons/ShooterProjectile.h"
#include "Upgrade_RocketProjectileSwap.generated.h"

class AShooterWeapon;
class UUpgradeDefinition_RocketProjectileSwap;

UCLASS(BlueprintType, meta = (DisplayName = "Rocket Projectile Swap"))
class POLARITY_API UUpgrade_RocketProjectileSwap : public UUpgradeComponent
{
	GENERATED_BODY()

public:
	UUpgrade_RocketProjectileSwap();

protected:
	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual void OnLevelChanged(int32 OldLevel, int32 NewLevel) override;
	virtual void OnWeaponChanged(AShooterWeapon* OldWeapon, AShooterWeapon* NewWeapon) override;

private:
	TWeakObjectPtr<UUpgradeDefinition_RocketProjectileSwap> CachedDef;
	TMap<AShooterWeapon*, TSubclassOf<AShooterProjectile>> OriginalProjectileClasses;

	void ApplyToWeapon(AShooterWeapon* Weapon);
	void RestoreWeapon(AShooterWeapon* Weapon);
	void RestoreAllWeapons();
	bool IsEligibleWeapon(const AShooterWeapon* Weapon) const;
};
