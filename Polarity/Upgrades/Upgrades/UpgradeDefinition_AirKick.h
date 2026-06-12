// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "Sound/SoundBase.h"
#include "NiagaraSystem.h"
#include "UpgradeDefinition_AirKick.generated.h"

class UDamageType;

/**
 * Data asset for the "Air Mail" upgrade (class kept as AirKick for asset compatibility).
 *
 * New behavior (full rework):
 *  1. BOUNCE: anything the player throws (yanked weapons, launched props, launched NPCs)
 *     that hits an enemy or a surface at a near-perpendicular angle (60–120° to the surface,
 *     i.e. within ±(90 − MinBounceAngleDeg) of the normal) — and didn't explode / survived —
 *     bounces off and flies to a point at the player's head height (slightly above).
 *  2. KICK: if the player air-melees the incoming object, it is redirected along the camera
 *     forward at KickSpeed and deals KickDamage to whatever it slams into.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_AirKick : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	// ==================== Bounce (return to player) ====================

	/** Minimum impact angle to the SURFACE PLANE (degrees) for the bounce to trigger.
	 *  90 = perfectly perpendicular hit. 60 (default) accepts the 60–120° incidence band —
	 *  glancing/sliding impacts below this angle do not bounce. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Bounce", meta = (ClampMin = "0.0", ClampMax = "90.0", Units = "deg"))
	float MinBounceAngleDeg = 60.0f;

	/** Minimum pre-impact speed (cm/s) for the bounce — settling/rolling objects don't return. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Bounce", meta = (ClampMin = "0.0"))
	float MinBounceImpactSpeed = 400.0f;

	/** Flight speed (cm/s) of the returning object. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Bounce", meta = (ClampMin = "100.0"))
	float ReturnSpeed = 1100.0f;

	/** Vertical offset (cm) above the player camera for the return target point ("head level or
	 *  slightly above"). The point is captured at bounce time — no homing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Bounce", meta = (Units = "cm"))
	float ReturnTargetHeightOffset = 25.0f;

	/** Spin (deg/s) applied to bounced weapons/props for visual flair. 0 = none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Bounce", meta = (ClampMin = "0.0"))
	float ReturnSpinSpeed = 360.0f;

	/** Optional one-shot VFX at the bounce point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Bounce|Feedback")
	TObjectPtr<UNiagaraSystem> BounceFX = nullptr;

	/** Scale for BounceFX. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Bounce|Feedback", meta = (ClampMin = "0.1"))
	float BounceFXScale = 1.0f;

	/** Optional sound at the bounce point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Bounce|Feedback")
	TObjectPtr<USoundBase> BounceSound = nullptr;

	// ==================== Kick (air-melee redirect) ====================

	/** Speed (cm/s) of the redirected object after the air-melee kick. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick", meta = (ClampMin = "500.0"))
	float KickSpeed = 4500.0f;

	/** Damage dealt by the kicked object to the NPC it slams into. For a kicked NPC projectile,
	 *  BOTH the target and the projectile NPC take this damage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick", meta = (ClampMin = "0.0"))
	float KickDamage = 300.0f;

	/** Damage type for KickDamage. If null, falls back to UDamageType_Melee. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick")
	TSubclassOf<UDamageType> KickDamageType;

	/** Spin (deg/s) applied to kicked weapons/props. 0 = none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick", meta = (ClampMin = "0.0"))
	float KickSpinSpeed = 720.0f;

	// ==================== Kick Aim Assist (launch-time snap) ====================
	// At the MOMENT of the kick the launch direction snaps toward the best enemy near the
	// camera forward: the aim point leads the enemy's current velocity over the projected
	// flight time and compensates the gravity drop. This is a one-shot launch correction —
	// NOT mid-flight homing; after launch the body flies ballistically.

	/** Master switch for the launch-time aim assist. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick Aim Assist")
	bool bEnableKickAimAssist = true;

	/** Max angle (deg) between camera forward and the enemy for the snap to engage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick Aim Assist", meta = (ClampMin = "2.0", ClampMax = "60.0", Units = "deg"))
	float KickAimAssistConeHalfAngleDeg = 20.0f;

	/** Max distance (cm) from the camera to the assisted enemy. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick Aim Assist", meta = (ClampMin = "200.0", Units = "cm"))
	float KickAimAssistRange = 3500.0f;

	// ==================== Kick Magnet (timing assist) ====================
	// When the player starts an AIRBORNE melee swing while an incoming object flies at them
	// (and they are facing it within MagnetConeHalfAngleDeg), the object's velocity is smoothly
	// steered so it arrives at the kick hitbox exactly when the damage window opens
	// (UAnimNotifyState_MeleeDamageWindow / UAnimNotify_MeleeActivate in the melee montage).

	/** Master switch for the kick timing assist. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick Magnet")
	bool bEnableKickMagnet = true;

	/** Max angle (deg) between camera forward and the direction to the incoming object for the
	 *  magnet to engage — the player must be facing the body. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick Magnet", meta = (ClampMin = "5.0", ClampMax = "90.0", Units = "deg"))
	float MagnetConeHalfAngleDeg = 30.0f;

	/** Search radius (cm) around the player for incoming objects when the swing starts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick Magnet", meta = (ClampMin = "100.0", Units = "cm"))
	float MagnetSearchRadius = 2500.0f;

	/** Where the kick hitbox point is, along camera forward (cm). The object is steered to
	 *  arrive at this point when the damage window opens (melee AttackRange is ~150). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick Magnet", meta = (ClampMin = "30.0", ClampMax = "300.0", Units = "cm"))
	float MagnetHitboxDistance = 90.0f;

	/** How quickly the object's velocity blends toward the steering velocity (VInterpTo speed).
	 *  Higher = snappier correction, lower = smoother arcs. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick Magnet", meta = (ClampMin = "1.0", ClampMax = "50.0"))
	float MagnetVelocityInterpSpeed = 10.0f;

	/** Hard cap (cm/s) on the steering velocity — prevents teleport-feel when the swing started
	 *  very late and the object would need absurd speed to make the window. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick Magnet", meta = (ClampMin = "500.0"))
	float MagnetMaxSpeed = 3500.0f;

	/** Lead time (seconds) used when the montage has no recognizable damage-window notify
	 *  (fallback arrival time target). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick Magnet", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MagnetFallbackLeadTime = 0.25f;

	// ==================== Kick Feedback ====================

	/** One-shot Niagara effect spawned at the kick contact point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick|Feedback")
	TObjectPtr<UNiagaraSystem> KickImpactFX = nullptr;

	/** Scale for the kick impact VFX. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick|Feedback", meta = (ClampMin = "0.1"))
	float KickImpactFXScale = 1.0f;

	/** Sound played at the kick contact point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick|Feedback")
	TObjectPtr<USoundBase> KickSound = nullptr;

	/** Volume multiplier for the kick sound. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick|Feedback", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float KickSoundVolume = 1.0f;

	/** Pitch multiplier for the kick sound. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Mail|Kick|Feedback", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float KickSoundPitch = 1.0f;
};
