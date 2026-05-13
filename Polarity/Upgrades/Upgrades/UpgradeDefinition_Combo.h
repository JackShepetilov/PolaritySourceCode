// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_Combo.generated.h"

class UCurveFloat;

/**
 * Data asset for the "Combo" upgrade.
 *
 * Each successful melee hit within ResetWindow seconds of the previous one
 * increments a combo counter. The counter feeds ComboCountToMultiplier curve
 * (X = count, Y = play rate multiplier) which is applied to both the fist
 * (UMeleeAttackComponent) and the sword (AShooterWeapon_Melee) so attacks
 * fire faster as the chain grows.
 *
 * Reset conditions (any one fires):
 *  - ResetWindow seconds elapsed without a new hit
 *  - A miss occurred (OnMeleeAttackEnded with bHasHitThisAttack=false)
 *
 * Charged Punch and Drop Kick increment the counter by their multi-kill
 * count (1 per killed target) to keep flow rewarding.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_Combo : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	// ==================== Combo Window ====================

	/** Seconds after the last successful hit before the combo resets to 0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combo|Timing", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float ResetWindow = 1.5f;

	/**
	 * Curve mapping combo count (X axis, 0..N) to play rate multiplier (Y axis).
	 * Default expectation: X=0 -> Y=1.0 (no combo, normal speed), then a non-linear
	 * ramp up toward a cap. Designer tunes in the asset editor. If null, the upgrade
	 * uses a fallback linear ramp (see Upgrade_Combo::EvaluateMultiplier).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combo|Scaling")
	TObjectPtr<UCurveFloat> ComboCountToMultiplier;

	/** Hard upper cap on the multiplier in case the curve is misconfigured. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combo|Scaling", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float MaxMultiplier = 3.0f;

	// ==================== Reset Behaviour ====================

	/** If true, missing a swing (no hit during damage window) immediately resets the combo to 0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combo|Reset")
	bool bResetOnMiss = true;
};
