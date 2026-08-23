// WeaponSpreadConfig.h
// How wide a weapon shoots, as one number the whole game agrees on.
//
// The spread is an ANGLE in degrees: the half-angle of the cone a bullet may leave in. An angle
// (rather than an offset at some distance) is the only form that keeps its meaning at every range,
// and it is also the only form the crosshair can be drawn from honestly: a cone of A degrees
// projects to a circle of radius (tan A / tan HalfFOV) * (viewport width / 2) pixels, so the ring
// on screen IS the region the shot can land in. See UCrosshairWidget::ComputeSpreadRadiusPixels.
//
// The number is built in two parts, and they are different things:
//
//   STATE   - where the player is and what they are doing. Standing still is the weapon's base
//             spread; crouching tightens it, walking/sprinting/jumping/sliding open it up; aiming
//             down sights tightens it hardest. This part is a MULTIPLIER on the weapon's base
//             spread (AShooterWeapon::AimVariance), so a weapon that was already tuned keeps its
//             character: an accurate rifle stays accurate while sprinting, relative to an SMG.
//             It chases its target rather than snapping, so stopping still costs a moment.
//
//   BLOOM   - what the trigger did. Every shot ADDS degrees, up to a ceiling, and they bleed back
//             off after a short delay. This part is added, not multiplied, so a burst opens even a
//             pinpoint weapon by a known amount.
//
// Total = clamp(Base * StateMultiplier + Bloom, 0, MaxSpreadDegrees).

#pragma once

#include "CoreMinimal.h"
#include "WeaponSpreadConfig.generated.h"

/**
 * Per-weapon spread tuning. Lives on AShooterWeapon next to FCrosshairConfig; the weapon's own
 * AimVariance is the base value every multiplier below acts on.
 */
USTRUCT(BlueprintType)
struct FWeaponSpreadConfig
{
	GENERATED_BODY()

	// ==================== State multipliers ====================
	//
	// Applied to the weapon's base spread. Exactly ONE of these wins per frame (the states are
	// resolved by priority: air > slide > sprint > crouch > walk > still), and then ADS scales
	// whatever came out. Stacking them would make a crouch-walk quieter or louder than either
	// state on its own for no reason a player could read off the screen.

	/** Standing still on the ground. 1.0 = exactly the weapon's own AimVariance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|State", meta = (ClampMin = "0.0"))
	float StillMultiplier = 1.0f;

	/** Crouching (and not sliding). The one state that TIGHTENS the hip spread. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|State", meta = (ClampMin = "0.0"))
	float CrouchMultiplier = 0.6f;

	/** Walking / normal ground movement, reached at WalkFullSpeed and interpolated from
	 *  StillMultiplier below it, so a nudge of the stick is not a full penalty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|State", meta = (ClampMin = "0.0"))
	float WalkMultiplier = 1.6f;

	/** Ground speed at which WalkMultiplier is fully reached. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|State", meta = (ClampMin = "1.0", Units = "cm/s"))
	float WalkFullSpeed = 400.0f;

	/** Sprinting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|State", meta = (ClampMin = "0.0"))
	float SprintMultiplier = 2.6f;

	/** Sliding. Between crouch and sprint on purpose: the player is low but moving fast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|State", meta = (ClampMin = "0.0"))
	float SlideMultiplier = 1.8f;

	/** Airborne: jumping, falling, wall-running. The widest state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|State", meta = (ClampMin = "0.0"))
	float AirMultiplier = 3.0f;

	/** Multiplies whatever state came out, blended in by the character's ADS alpha, so the spread
	 *  tightens over the same time the sight comes up rather than snapping at the button press. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|State", meta = (ClampMin = "0.0"))
	float AdsMultiplier = 0.25f;

	/** How fast the state part follows a state change (interp speed, per second). Low values make
	 *  stopping to shoot cost a beat; very high values make it instant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|State", meta = (ClampMin = "0.1"))
	float StateInterpSpeed = 9.0f;

	// ==================== Firing bloom ====================

	/** Degrees added to the spread by one shot. Pellets do not each count: one trigger pull is one
	 *  addition (see AShooterWeapon::AddShotSpread). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Bloom", meta = (ClampMin = "0.0", Units = "deg"))
	float PerShotDegrees = 0.35f;

	/** Ceiling on the bloom alone, so holding the trigger settles at a known worst case. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Bloom", meta = (ClampMin = "0.0", Units = "deg"))
	float MaxBloomDegrees = 3.0f;

	/** Quiet time after the last shot before the bloom starts coming off. Roughly one refire
	 *  interval keeps a held trigger from recovering between its own shots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Bloom", meta = (ClampMin = "0.0", Units = "s"))
	float BloomRecoveryDelay = 0.15f;

	/** How fast the bloom bleeds off once the delay has passed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Bloom", meta = (ClampMin = "0.0", Units = "deg/s"))
	float BloomRecoveryRate = 4.0f;

	/** Scales the per-shot addition while aiming down sights, blended by the ADS alpha. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Bloom", meta = (ClampMin = "0.0"))
	float AdsBloomMultiplier = 0.5f;

	// ==================== Ceiling ====================

	/** Hard ceiling on state + bloom together. Also the number the crosshair can never exceed, so
	 *  it cannot grow off the screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread", meta = (ClampMin = "0.0", ClampMax = "45.0", Units = "deg"))
	float MaxSpreadDegrees = 12.0f;

	/** Whether an AI holding this weapon gets the state and bloom treatment too.
	 *
	 *  Off, because enemy accuracy is tuned somewhere else entirely: an NPC already aims through its
	 *  own AimVarianceHalfAngle, and quietly tripling how wide it shoots whenever it happens to be
	 *  sprinting would be a difficulty change hiding inside a crosshair feature. An AI owner shoots
	 *  at the weapon's plain AimVariance, exactly as it did before this system existed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	bool bApplyToAIOwners = false;
};
