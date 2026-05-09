// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_DropKick.generated.h"

/**
 * Data asset for the "Drop Kick" unlock upgrade.
 * Grants the player the ability to drop-kick from the air. No tuning fields yet —
 * the kick itself is configured on the MeleeAttackComponent's FMeleeAttackSettings.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_DropKick : public UUpgradeDefinition
{
	GENERATED_BODY()
};
