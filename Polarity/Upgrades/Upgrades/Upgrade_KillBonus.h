// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_KillBonus.generated.h"

class UUpgradeDefinition_KillBonus;

/**
 * Runtime for the kill-bonus passives. One class for all three kinds, because all three are the same
 * shape: a kill starts a clock, and one thing is true while the clock runs.
 * @see UUpgradeDefinition_KillBonus
 */
UCLASS(BlueprintType, meta = (DisplayName = "Kill Bonus"))
class POLARITY_API UUpgrade_KillBonus : public UUpgradeComponent
{
	GENERATED_BODY()

public:
	UUpgrade_KillBonus();

	/** True while the timed bonus from the last kill is still running. */
	UFUNCTION(BlueprintPure, Category = "Kill Bonus")
	bool IsBonusActive() const;

protected:
	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual void OnOwnerDealtDamage(AActor* Target, float Damage, bool bKilled) override;
	virtual void OnOwnerTookDamage(float Damage, AActor* DamageCauser) override;
	virtual float GetReloadSpeedMultiplier() const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	TWeakObjectPtr<UUpgradeDefinition_KillBonus> CachedDef;

	/** World time the running bonus ends. */
	float BonusEndTime = -1.0f;
};
