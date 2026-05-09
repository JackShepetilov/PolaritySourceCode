// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_AirDash.generated.h"

/**
 * "Air Dash" Upgrade — pure unlock.
 *
 * On activation: sets APolarityCharacter::bCanAirDash to true.
 * On deactivation: sets it back to false.
 *
 * The actual dash logic, charges, cooldown and redirect parameters live on the
 * owner's ApexMovementComponent / MovementSettings.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Air Dash"))
class POLARITY_API UUpgrade_AirDash : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_AirDash();

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
};
