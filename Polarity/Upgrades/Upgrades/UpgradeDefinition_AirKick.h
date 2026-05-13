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
 * the prop is forced to detonate in place. Damage from the explosion is a fixed
 * value set on this definition, independent of the prop's own ExplosionDamage and
 * any charge-based scaling — so the kick output is predictable regardless of which
 * prop the player chooses to detonate.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_AirKick : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	// ==================== Trigger Conditions ====================

	/** Maximum distance below the prop to trace for ground (cm).
	 *  If ground is found within this distance, the prop is considered grounded and the kick won't trigger. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick", meta = (ClampMin = "10.0"))
	float PropAirborneTraceDistance = 80.0f;

	// ==================== Forced Explosion ====================

	/**
	 * Flat damage dealt by the forced explosion. Overrides the prop's own
	 * ExplosionDamage and bypasses charge-based scaling — every air-kick deals
	 * exactly this much regardless of which prop is hit.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|Explosion", meta = (ClampMin = "0.0"))
	float FixedExplosionDamage = 100.0f;

	/**
	 * Multiplier applied to the prop's ExplosionRadius for this detonation only.
	 * 1.0 keeps the prop's configured radius; >1 makes the air-kick blast bigger,
	 * <1 makes it smaller. Doesn't change the prop's base ExplosionRadius.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|Explosion", meta = (ClampMin = "0.1"))
	float ExplosionRadiusMultiplier = 1.0f;

	/**
	 * Multiplier applied to the explosion's VFX scale. Cosmetic only.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|Explosion", meta = (ClampMin = "0.1"))
	float ExplosionVFXScaleMultiplier = 1.0f;

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
