// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_AirDash.generated.h"

/**
 * "Air Dash" upgrade, a jump-slot upgrade: jump pressed in the air dashes instead of jumping.
 *
 * Holds no state and writes nothing. The movement simulation asks GetJumpSlotParams on every
 * machine that simulates the move and reads the charges, cooldown and speed from the level data.
 *
 * It used to write those numbers into the shared MovementSettings asset, which in co-op handed
 * one player's dash to everybody using the same asset. Nothing is written now.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Air Dash"))
class POLARITY_API UUpgrade_AirDash : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_AirDash();

	virtual bool GetJumpSlotParams(FJumpSlotParams& Out) const override;

protected:

	/** Only the HUD flag: AbilityResourceBar shows the dash entry while it is set. */
	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
};
