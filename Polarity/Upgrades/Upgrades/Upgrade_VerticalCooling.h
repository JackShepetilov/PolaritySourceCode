// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_VerticalCooling.generated.h"

class UUpgradeDefinition_VerticalCooling;

/**
 * Vertical Cooling upgrade.
 *
 * Tracks absolute Z movement. Each counted meter restores HP immediately, up to
 * a heal pool that resets on a fixed interval.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Vertical Cooling"))
class POLARITY_API UUpgrade_VerticalCooling : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_VerticalCooling();

	UFUNCTION(BlueprintPure, Category = "Vertical Cooling")
	float GetRemainingHealPool() const { return RemainingHealPool; }

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	void RefreshHealPool();

	TWeakObjectPtr<UUpgradeDefinition_VerticalCooling> CachedDef;

	float RemainingHealPool = 0.0f;
	float PoolRefreshElapsed = 0.0f;
	float PreviousZ = 0.0f;
	bool bHasPreviousZ = false;
};
