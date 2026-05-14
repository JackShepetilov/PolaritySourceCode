// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_Backstab.generated.h"

class UUpgradeDefinition_Backstab;

/**
 * Runtime for the Backstab upgrade. Overrides GetMeleeDamageMultiplier to apply a
 * level-defined multiplier when the target is stunned AND the player is in that
 * target's back cone. See UpgradeDefinition_Backstab.h for design.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Backstab"))
class POLARITY_API UUpgrade_Backstab : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_Backstab();

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual void OnLevelChanged(int32 OldLevel, int32 NewLevel) override;

	virtual float GetMeleeDamageMultiplier(AActor* Target) const override;

private:

	TWeakObjectPtr<UUpgradeDefinition_Backstab> CachedDef;

	/** True iff Target is an AShooterNPC currently in a stunned state per the level's gate. */
	bool IsTargetStunned(AActor* Target) const;

	/** True iff the owning player is positioned inside Target's back cone. */
	bool IsPlayerBehindTarget(AActor* Target) const;
};
