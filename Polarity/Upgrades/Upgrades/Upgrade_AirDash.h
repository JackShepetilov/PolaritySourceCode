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
	virtual void OnLevelChanged(int32 OldLevel, int32 NewLevel) override;

private:

	/** Reads UpgradeDefinition_AirDash.LevelData[Level-1] and applies it to MovementSettings. */
	void ApplyForLevel(int32 Level);

	/** Snapshot the current MovementSettings values so OnUpgradeDeactivated can restore them. */
	void CaptureBaseline();

	/** Restore original MovementSettings values captured in OnUpgradeActivated. */
	void RevertToBaseline();

	// Cached baseline of MovementSettings fields we modify (set in OnUpgradeActivated).
	int32 BaselineMaxAirDashCount = 1;
	float BaselineAirDashCooldown = 1.5f;
	float BaselineAirDashSpeed = 800.0f;
	bool bBaselineCaptured = false;
};
