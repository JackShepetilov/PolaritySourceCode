// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_SMGAmmoRefund.generated.h"

class AShooterWeapon;
class UUpgradeDefinition_SMGAmmoRefund;

UCLASS(BlueprintType, meta = (DisplayName = "SMG Ammo Refund"))
class POLARITY_API UUpgrade_SMGAmmoRefund : public UUpgradeComponent
{
	GENERATED_BODY()

public:
	UUpgrade_SMGAmmoRefund();

protected:
	virtual void OnUpgradeActivated() override;
	virtual void OnWeaponDealtDamage(AShooterWeapon* Weapon, AActor* Target, float Damage, bool bKilled) override;

private:
	TWeakObjectPtr<UUpgradeDefinition_SMGAmmoRefund> CachedDef;

	bool IsEligibleWeapon(const AShooterWeapon* Weapon) const;
};
