// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "Sound/SoundBase.h"
#include "UpgradeDefinition_SuppressionFire.generated.h"

/**
 * Data asset for the "Suppression Fire" upgrade.
 * Hitscan hits on ranged enemies (ShooterNPC) suppress their accuracy,
 * forcing a donut-pattern miss around the player. Duration scales with player speed.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_SuppressionFire : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	// ==================== Duration Tuning ====================

	/** Suppression duration at minimum speed threshold (seconds) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float MinSuppressionDuration = 0.5f;

	/** Suppression duration at max speed (seconds) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float MaxSuppressionDuration = 3.0f;

	// ==================== Speed Tuning ====================

	/** Minimum player speed to trigger suppression (cm/s). Below this, no effect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire", meta = (ClampMin = "0.0"))
	float MinSpeedThreshold = 100.0f;

	/** Player speed for full suppression duration (cm/s). Scales linearly from MinSpeed to this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire", meta = (ClampMin = "100.0"))
	float MaxSpeedForFullEffect = 1200.0f;

	// ==================== Stacking ====================

	/** Diminishing returns factor for stacking. Higher = faster diminishing.
	 *  Each stack adds: Duration / (1 + StackCount * Factor) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float DiminishingReturnsFactor = 0.5f;

	// ==================== Sound ====================

	/** Sound played on the enemy when Plot Armor activates.
	 *  Volume, pitch and reverb are scaled by player speed at the moment of the hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire|Sound")
	TObjectPtr<USoundBase> SuppressionSound = nullptr;

	/** Volume multiplier at minimum speed */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire|Sound", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float SoundVolumeMin = 0.4f;

	/** Volume multiplier at maximum speed */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire|Sound", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float SoundVolumeMax = 1.0f;

	/** Pitch multiplier at minimum speed */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire|Sound", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float SoundPitchMin = 0.8f;

	/** Pitch multiplier at maximum speed */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire|Sound", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float SoundPitchMax = 1.2f;

	/** Reverb send level at minimum speed (0.0 = dry, 1.0 = fully wet).
	 *  Requires the Sound Cue to have a float parameter named "ReverbSend". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire|Sound", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SoundReverbMin = 0.0f;

	/** Reverb send level at maximum speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suppression Fire|Sound", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SoundReverbMax = 0.6f;
};
