// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_Combo.generated.h"

class UCurveFloat;

/**
 * Per-level tuning block. Designer fills out one entry per level (index = CurrentLevel - 1).
 * Set MaxLevel on the parent UpgradeDefinition to N and add N entries here to match.
 */
USTRUCT(BlueprintType)
struct FComboLevelData
{
	GENERATED_BODY()

	/** Seconds after the last successful hit before the combo resets to 0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combo", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float ResetWindow = 1.5f;

	/**
	 * Curve mapping combo count (X axis, 0..N) to play rate multiplier (Y axis).
	 * Null → falls back to linear ramp (1.0 + 0.1 * count, clamped to MaxMultiplier).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combo")
	TObjectPtr<UCurveFloat> ComboCountToMultiplier;

	/** Hard upper cap on the multiplier in case the curve is misconfigured. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combo", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float MaxMultiplier = 3.0f;

	/** If true, missing a swing (no hit during damage window) immediately resets the combo to 0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combo")
	bool bResetOnMiss = true;
};

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

	/**
	 * Per-level tuning. Index 0 = level 1, index 1 = level 2, etc.
	 * MaxLevel on the parent definition should match the number of entries.
	 * If CurrentLevel exceeds the array, the last entry is reused.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combo|Levels")
	TArray<FComboLevelData> LevelData;

	/** Returns the tuning block for the given level (1-based), safely clamped. */
	const FComboLevelData& GetLevelData(int32 Level) const
	{
		if (LevelData.Num() == 0)
		{
			static const FComboLevelData Default;
			return Default;
		}
		const int32 Index = FMath::Clamp(Level - 1, 0, LevelData.Num() - 1);
		return LevelData[Index];
	}
};
