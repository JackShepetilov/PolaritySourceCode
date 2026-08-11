// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_MaxHealth.generated.h"

class UUpgradeDefinition_MaxHealth;

UCLASS(BlueprintType, meta = (DisplayName = "Max Health"))
class POLARITY_API UUpgrade_MaxHealth : public UUpgradeComponent
{
	GENERATED_BODY()

public:
	UUpgrade_MaxHealth();

protected:
	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual void OnLevelChanged(int32 OldLevel, int32 NewLevel) override;

private:
	void ApplyForLevel(int32 Level);

	TWeakObjectPtr<UUpgradeDefinition_MaxHealth> CachedDef;
	float AppliedMaxHPBonus = 0.0f;
};
