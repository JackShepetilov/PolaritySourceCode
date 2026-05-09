// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_AirDash.generated.h"

/**
 * Data asset for the "Air Dash" unlock upgrade.
 * Grants the player the ability to air-dash. No tuning fields yet — the dash
 * itself is configured on the ApexMovementComponent / MovementSettings.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_AirDash : public UUpgradeDefinition
{
	GENERATED_BODY()
};
