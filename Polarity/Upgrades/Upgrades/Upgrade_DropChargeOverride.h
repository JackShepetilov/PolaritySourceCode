// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_DropChargeOverride.generated.h"

class ADroppedRangedWeapon;
class UUpgradeDefinition_DropChargeOverride;

UCLASS(BlueprintType, meta = (DisplayName = "Drop Charge Override"))
class POLARITY_API UUpgrade_DropChargeOverride : public UUpgradeComponent
{
	GENERATED_BODY()

public:
	UUpgrade_DropChargeOverride();

protected:
	virtual void OnUpgradeActivated() override;
	virtual void OnEnemyDroppedRangedWeapon(ADroppedRangedWeapon* DroppedWeapon, AActor* DroppingEnemy) override;

private:
	TWeakObjectPtr<UUpgradeDefinition_DropChargeOverride> CachedDef;
};
