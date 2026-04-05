// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "Sound/SoundBase.h"
#include "NiagaraSystem.h"
#include "UpgradeDefinition_AirKick.generated.h"

/**
 * Data asset for the "Air Kick" upgrade.
 * When the player hits an airborne EMFPhysicsProp with melee while also airborne,
 * triggers an instant capture+launch cycle: the prop is kicked in the camera's
 * forward direction and the player's polarity toggles. No channeling animation plays.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_AirKick : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	// ==================== Launch Tuning ====================

	/** Speed at which the kicked prop is launched (cm/s).
	 *  Must be high enough to deal collision damage on impact (>1500 for explosion). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick", meta = (ClampMin = "500.0"))
	float LaunchSpeed = 3000.0f;

	/** Maximum distance below the prop to trace for ground (cm).
	 *  If ground is found within this distance, the prop is considered grounded and the kick won't trigger. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick", meta = (ClampMin = "10.0"))
	float PropAirborneTraceDistance = 80.0f;

	/** Spin speed applied to the prop on kick (degrees/second). Zero = no spin. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick", meta = (ClampMin = "0.0"))
	float KickSpinSpeed = 720.0f;

	// ==================== VFX ====================

	/** One-shot Niagara effect spawned at kick point */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|VFX")
	TObjectPtr<UNiagaraSystem> KickImpactFX = nullptr;

	/** Scale for the impact VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|VFX", meta = (ClampMin = "0.1"))
	float KickImpactFXScale = 1.0f;

	// ==================== Sound ====================

	/** Sound played at kick impact point */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|Sound")
	TObjectPtr<USoundBase> KickSound = nullptr;

	/** Volume multiplier for kick sound */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|Sound", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float KickSoundVolume = 1.0f;

	/** Pitch multiplier for kick sound */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|Sound", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float KickSoundPitch = 1.0f;
};
