// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_Bandolier.generated.h"

class UUpgradeDefinition_Bandolier;

/**
 * "Bandolier" upgrade — extra slots for yanked-weapon copies of the same class.
 *
 * Data-only component: holds the cached definition pointer so external systems
 * (DroppedRangedWeapon pickup, ThrowYankedWeaponIfAny) can query MaxCopies for the
 * current level via UpgradeManager->GetBandolierMaxCopies. No tick, no event hooks —
 * the pickup/discard pipelines drive all behavior.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Bandolier"))
class POLARITY_API UUpgrade_Bandolier : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_Bandolier();

	/** Max copies of the same yanked class the player may carry at the CURRENT level.
	 *  Returns 1 (no expansion) if the definition is missing. */
	int32 GetMaxCopiesForCurrentLevel() const;

protected:

	virtual void OnUpgradeActivated() override;

private:

	TWeakObjectPtr<UUpgradeDefinition_Bandolier> DefBandolier;
};
