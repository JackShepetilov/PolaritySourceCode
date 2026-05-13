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
 * the prop is launched forward in the camera direction (same as the previous
 * reverse-channeling kick). The kick also marks the prop so that any subsequent
 * collision with an NPC during flight detonates it with a fixed damage value
 * defined here — so the kick's payload is predictable regardless of the prop's
 * own ExplosionDamage / bCanExplode / charge-scaling configuration.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_AirKick : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	// ==================== Launch Tuning ====================

	/** Speed at which the kicked prop is launched (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick", meta = (ClampMin = "500.0"))
	float LaunchSpeed = 3000.0f;

	/** Maximum distance below the prop to trace for ground (cm).
	 *  If ground is found within this distance, the prop is considered grounded and the kick won't trigger. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick", meta = (ClampMin = "10.0"))
	float PropAirborneTraceDistance = 80.0f;

	/** Spin speed applied to the prop on kick (degrees/second). Zero = no spin. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick", meta = (ClampMin = "0.0"))
	float KickSpinSpeed = 720.0f;

	// ==================== On-Impact Explosion ====================

	/**
	 * Flat damage dealt by the explosion when the launched prop hits an NPC.
	 * Overrides the prop's own ExplosionDamage and bypasses charge-based scaling —
	 * every air-kick payload is the same regardless of which prop was kicked.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|Explosion", meta = (ClampMin = "0.0"))
	float FixedExplosionDamage = 100.0f;

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
