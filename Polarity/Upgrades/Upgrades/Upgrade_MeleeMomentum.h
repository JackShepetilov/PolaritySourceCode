// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_MeleeMomentum.generated.h"

/**
 * Multiplies melee base damage by how fast the owner was closing on the target when the swing
 * began. Stateless: everything is worked out when the multiplier is asked for.
 * @see UUpgradeDefinition_MeleeMomentum
 */
UCLASS(BlueprintType, meta = (DisplayName = "Melee Momentum"))
class POLARITY_API UUpgrade_MeleeMomentum : public UUpgradeComponent
{
	GENERATED_BODY()

protected:
	virtual float GetMeleeDamageMultiplier(AActor* Target) const override;
};
