// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_ChargedPunch.generated.h"

class UCurveFloat;
class UNiagaraSystem;
class USoundBase;
class UCameraShakeBase;
class UDamageType;

/**
 * Data asset for the "Charged Punch" upgrade.
 *
 * While the melee button is held past MinHoldTime, drain the shared health-pickup
 * pool at PickupsPerSecond. On release, fling the player forward toward a
 * camera-aimed end point; do a capsule sweep along the trajectory dealing
 * damage to all enemies on the line (pierces through — no first-hit blocking).
 *
 * Distance is read from HoldTimeToDistance (X = hold seconds, Y = cm). If the
 * curve is null, fallback linear is used (MaxDistance reached at MaxHoldTime).
 * If the camera-forward trace from the player hits geometry before that distance,
 * the lunge endpoint is clamped at the wall (so the player lands next to it).
 *
 * Damage is BaseDamage (Settings.BaseDamage from the melee component) + a
 * hold-time bonus (HoldTimeToBonusDamage curve, fallback linear up to MaxBonusDamage).
 * Each target hit gets electrified (charge -25) and knockback BaseKnockbackDistance,
 * matching the regular melee defaults.
 *
 * Multi-kill counts feed the Combo upgrade (one combo hit per killed target).
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_ChargedPunch : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	// ==================== Hold Threshold & Drain ====================

	/**
	 * Seconds the button must be held before the charge "really starts" (VFX appears,
	 * pickups start draining). Releases under this threshold do nothing — the regular
	 * jab fired off Triggered is the only outcome.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|Timing", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MinHoldTime = 0.15f;

	/** Maximum hold time (seconds). Charge stops scaling past this point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|Timing", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float MaxHoldTime = 1.5f;

	/** Pickups consumed per second while charging (past MinHoldTime). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|Cost", meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float PickupsPerSecond = 3.0f;

	// ==================== Distance & Damage ====================

	/** Curve mapping hold time (X seconds, 0..MaxHoldTime) to lunge distance (Y cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|Distance")
	TObjectPtr<UCurveFloat> HoldTimeToDistance;

	/** Linear fallback if HoldTimeToDistance is null: distance reached at MaxHoldTime. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|Distance", meta = (ClampMin = "100.0", ClampMax = "3000.0"))
	float MaxDistance = 1000.0f;

	/** Curve mapping hold time to bonus damage (Y units of damage). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|Damage")
	TObjectPtr<UCurveFloat> HoldTimeToBonusDamage;

	/** Linear fallback bonus damage at MaxHoldTime if HoldTimeToBonusDamage is null. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|Damage", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float MaxBonusDamage = 100.0f;

	/** Capsule radius (cm) used for the pierce-through sweep along the lunge path. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|Sweep", meta = (ClampMin = "20.0", ClampMax = "200.0"))
	float CapsuleRadius = 60.0f;

	/**
	 * Damage type for the punch. Defaults to DamageType_Melee in BP so XP routes
	 * to the Melee skill via XPConfig.KillXPRouting.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|Damage")
	TSubclassOf<UDamageType> DamageType;

	// ==================== Visual / Audio ====================

	/**
	 * VFX continuously spawned at the predicted endpoint while charging.
	 * Updates each tick to track where the punch will land.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|FX")
	TObjectPtr<UNiagaraSystem> EndpointPreviewVFX;

	/** VFX played on the player when the punch fires (release). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|FX")
	TObjectPtr<UNiagaraSystem> FireVFX;

	/** VFX spawned on each hit target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|FX")
	TObjectPtr<UNiagaraSystem> HitVFX;

	/** Sound played while charging (looped — stop on release/cancel). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|Audio")
	TObjectPtr<USoundBase> ChargeLoopSound;

	/** Sound played on release. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|Audio")
	TObjectPtr<USoundBase> FireSound;

	/** Sound on hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|Audio")
	TObjectPtr<USoundBase> HitSound;

	/** Camera shake scale on release. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|Camera")
	TSubclassOf<UCameraShakeBase> FireCameraShake;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ChargedPunch|Camera", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float FireCameraShakeScale = 1.0f;
};
