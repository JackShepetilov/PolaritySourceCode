// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PolarityCharacter.h"
#include "ShooterWeaponHolder.h"
#include "ApexMovementComponent.h"
#include "ShooterCharacter.generated.h"

class AShooterWeapon;
class UInputAction;
class UInputComponent;
class UPawnNoiseEmitterComponent;
class UWeaponRecoilComponent;
class UHitMarkerComponent;
class UMeleeAttackComponent;
class UChargeAnimationComponent;
class UAudioComponent;
class UCurveFloat;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBulletCountUpdatedDelegate, int32, MagazineSize, int32, Bullets);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDamagedDelegate, float, LifePercent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDamageDirectionDelegate, float, AngleDegrees, float, Damage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHeatUpdatedDelegate, float, HeatPercent, float, DamageMultiplier);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSpeedUpdatedDelegate, float, SpeedPercent, float, CurrentSpeed, float, MaxSpeed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPolarityChangedDelegate, uint8, NewPolarity, float, ChargeValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FChargeUpdatedDelegate, float, ChargeValue, uint8, Polarity);

/**
 *  A player controllable first person shooter character
 *  Manages a weapon inventory through the IShooterWeaponHolder interface
 *  Manages health and death
 */
UCLASS(abstract)
class POLARITY_API AShooterCharacter : public APolarityCharacter, public IShooterWeaponHolder
{
	GENERATED_BODY()

	/** AI Noise emitter component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UPawnNoiseEmitterComponent* PawnNoiseEmitter;

	/** Advanced weapon recoil component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UWeaponRecoilComponent* RecoilComponent;

	/** Hit marker and kill confirm component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UHitMarkerComponent* HitMarkerComponent;

	/** Melee attack component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UMeleeAttackComponent* MeleeAttackComponent;

	/** Charge animation component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UChargeAnimationComponent* ChargeAnimationComponent;

protected:

	/** Fire weapon input action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* FireAction;

	/** Switch weapon input action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SwitchWeaponAction;

	/** Aim down sights input action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* ADSAction;

	/** Melee attack input action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MeleeAction;

	/** Name of the first person mesh weapon socket */
	UPROPERTY(EditAnywhere, Category = "Weapons")
	FName FirstPersonWeaponSocket = FName("HandGrip_R");

	/** Name of the third person mesh weapon socket */
	UPROPERTY(EditAnywhere, Category = "Weapons")
	FName ThirdPersonWeaponSocket = FName("HandGrip_R");

	/** Max distance to use for aim traces */
	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = 0, ClampMax = 100000, Units = "cm"))
	float MaxAimDistance = 10000.0f;

	// ==================== ADS State ====================

	/** True when player is holding ADS button */
	bool bWantsToAim = false;

	/** Current ADS alpha (0 = hip fire, 1 = fully aimed) */
	float CurrentADSAlpha = 0.0f;

	/** Base FOV of the camera (stored on BeginPlay) */
	float BaseCameraFOV = 90.0f;

	/** Base First Person FOV (stored on BeginPlay) */
	float BaseFirstPersonFOV = 70.0f;

	FVector BaseCameraLocation = FVector::ZeroVector;
	/** Max HP this character can have */
	UPROPERTY(EditAnywhere, Category = "Health")
	float MaxHP = 500.0f;

	/** Current HP remaining to this character */
	float CurrentHP = 0.0f;

	// ==================== HP Regeneration ====================

	/** If true, HP regenerates over time based on movement speed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Regeneration")
	bool bEnableRegeneration = true;

	/** Base HP regeneration rate when stationary (HP/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Regeneration", meta = (ClampMin = "0.0", EditCondition = "bEnableRegeneration"))
	float BaseRegenRate = 5.0f;

	/** Maximum HP regeneration rate at max speed (HP/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Regeneration", meta = (ClampMin = "0.0", EditCondition = "bEnableRegeneration"))
	float MaxRegenRate = 50.0f;

	/** Speed considered maximum for regeneration scaling (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Regeneration", meta = (ClampMin = "100.0", EditCondition = "bEnableRegeneration"))
	float MaxSpeedForRegen = 1200.0f;

	/** Delay after taking damage before regeneration starts (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Regeneration", meta = (ClampMin = "0.0", EditCondition = "bEnableRegeneration"))
	float RegenDelayAfterDamage = 2.0f;

	/**
	 * Optional curve for speed-to-regen mapping.
	 * X = normalized speed (0-1), Y = regen multiplier (0-1).
	 * If null, linear interpolation is used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Regeneration", meta = (EditCondition = "bEnableRegeneration"))
	TObjectPtr<UCurveFloat> SpeedToRegenCurve;

	/** Time since last damage was taken (for regen delay) */
	float TimeSinceLastDamage = 0.0f;

	/** Team ID for this character*/
	UPROPERTY(EditAnywhere, Category = "Team")
	uint8 TeamByte = 0;

	/** List of weapons picked up by the character */
	TArray<AShooterWeapon*> OwnedWeapons;

	/** Weapon currently equipped and ready to shoot with */
	TObjectPtr<AShooterWeapon> CurrentWeapon;

	UPROPERTY(EditAnywhere, Category = "Destruction", meta = (ClampMin = 0, ClampMax = 10, Units = "s"))
	float RespawnTime = 5.0f;

	FTimerHandle RespawnTimer;

	// ==================== UI Speed & Polarity ====================

	/** Maximum speed for UI normalization (cm/s) */
	UPROPERTY(EditAnywhere, Category = "UI", meta = (ClampMin = "100.0"))
	float MaxSpeedForUI = 1200.0f;

	/** Previous polarity state for change detection (0=Neutral, 1=Positive, 2=Negative) */
	uint8 PreviousPolarity = 0;

	// ==================== Mouse Input Tracking ====================

	/** Last frame's mouse delta for recoil sway */
	FVector2D LastMouseDelta = FVector2D::ZeroVector;

	// ==================== SFX|Footsteps ====================

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â® ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps")
	TObjectPtr<USoundBase> FootstepSound;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float FootstepPitchMin = 0.95f;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float FootstepPitchMax = 1.05f;

	/** ÃƒÆ’Ã†â€™ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float FootstepVolume = 1.0f;

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps")
	TObjectPtr<USoundBase> CrouchFootstepSound;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float CrouchFootstepPitchMin = 0.9f;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float CrouchFootstepPitchMax = 1.1f;

	/** ÃƒÆ’Ã†â€™ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float CrouchFootstepVolume = 0.5f;

	// ==================== SFX|Slide ====================

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Slide")
	TObjectPtr<USoundBase> SlideStartSound;

	/** ÃƒÆ’Ã¢â‚¬â€œÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â© ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Slide")
	TObjectPtr<USoundBase> SlideLoopSound;

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Slide")
	TObjectPtr<USoundBase> SlideEndSound;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Slide", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float SlideSoundPitchMin = 0.95f;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Slide", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float SlideSoundPitchMax = 1.05f;

	/** ÃƒÆ’Ã†â€™ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Slide", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float SlideSoundVolume = 1.0f;

	// ==================== SFX|WallRun ====================

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â  wallrun */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|WallRun")
	TObjectPtr<USoundBase> WallRunStartSound;

	/** ÃƒÆ’Ã¢â‚¬â€œÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â© ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª wallrun */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|WallRun")
	TObjectPtr<USoundBase> WallRunLoopSound;

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ wallrun */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|WallRun")
	TObjectPtr<USoundBase> WallRunEndSound;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ wallrun */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|WallRun", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float WallRunSoundPitchMin = 0.95f;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ wallrun */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|WallRun", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float WallRunSoundPitchMax = 1.05f;

	/** ÃƒÆ’Ã†â€™ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ wallrun */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|WallRun", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float WallRunSoundVolume = 1.0f;

	// ==================== SFX|Jump ====================

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â® ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â¦ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Jump")
	TObjectPtr<USoundBase> JumpSound;

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â® ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â¦ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Jump")
	TObjectPtr<USoundBase> DoubleJumpSound;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â¦ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Jump", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float JumpSoundPitchMin = 0.95f;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â¦ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Jump", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float JumpSoundPitchMax = 1.05f;

	/** ÃƒÆ’Ã†â€™ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â¦ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Jump", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float JumpSoundVolume = 1.0f;

	// ==================== SFX|Land ====================

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Land")
	TObjectPtr<USoundBase> LandSound;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Land", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float LandSoundPitchMin = 0.9f;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Land", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float LandSoundPitchMax = 1.1f;

	/** ÃƒÆ’Ã†â€™ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Land", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float LandSoundVolume = 1.0f;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  (ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¬/ÃƒÆ’Ã‚Â±) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Land", meta = (ClampMin = "0.0"))
	float LandSoundMinFallSpeed = 300.0f;

	// ==================== SFX Audio Components ====================

	/** Audio component ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â¶ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â® ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UPROPERTY()
	TObjectPtr<UAudioComponent> SlideLoopAudioComponent;

	/** Audio component ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â¶ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â® ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  wallrun */
	UPROPERTY()
	TObjectPtr<UAudioComponent> WallRunLoopAudioComponent;

public:

	/** Bullet count updated delegate */
	FBulletCountUpdatedDelegate OnBulletCountUpdated;

	/** Damaged delegate */
	FDamagedDelegate OnDamaged;

	/** Damage direction delegate (angle in degrees relative to player forward, 0 = front, 90 = right, 180 = back, -90 = left) */
	FDamageDirectionDelegate OnDamageDirection;

	/** Heat level updated delegate */
	FHeatUpdatedDelegate OnHeatUpdated;

	/** Speed updated delegate */
	FSpeedUpdatedDelegate OnSpeedUpdated;

	/** Polarity changed delegate (fires only when sign changes) */
	FPolarityChangedDelegate OnPolarityChanged;

	/** Charge updated delegate (fires every tick) */
	FChargeUpdatedDelegate OnChargeUpdated;

public:

	/** Constructor */
	AShooterCharacter();

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Gameplay cleanup */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

	/** Per-frame updates */
	virtual void Tick(float DeltaTime) override;

	/** Override to track mouse input for recoil sway */
	virtual void DoAim(float Yaw, float Pitch) override;

public:

	/** Handle incoming damage */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

public:

	/** Handles start firing input */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStartFiring();

	/** Handles stop firing input */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStopFiring();

	/** Handles switch weapon input */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoSwitchWeapon();

	/** Handles start ADS input */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStartADS();

	/** Handles stop ADS input */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStopADS();

	/** Handles melee attack input */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoMeleeAttack();

	/** Returns true if currently aiming down sights */
	UFUNCTION(BlueprintPure, Category = "ADS")
	bool IsAiming() const { return bWantsToAim; }

	/** Returns current ADS alpha (0-1) */
	UFUNCTION(BlueprintPure, Category = "ADS")
	float GetADSAlpha() const { return CurrentADSAlpha; }

	/** Returns the recoil component */
	UFUNCTION(BlueprintPure, Category = "Recoil")
	UWeaponRecoilComponent* GetRecoilComponent() const { return RecoilComponent; }

	/** Returns the hit marker component */
	UFUNCTION(BlueprintPure, Category = "Hit Marker")
	UHitMarkerComponent* GetHitMarkerComponent() const { return HitMarkerComponent; }

	/** Returns the melee attack component */
	UFUNCTION(BlueprintPure, Category = "Melee")
	UMeleeAttackComponent* GetMeleeAttackComponent() const { return MeleeAttackComponent; }

	/** Returns the charge animation component */
	UFUNCTION(BlueprintPure, Category = "Charge")
	UChargeAnimationComponent* GetChargeAnimationComponent() const { return ChargeAnimationComponent; }

	/** Returns the currently equipped weapon */
	UFUNCTION(BlueprintPure, Category = "Weapons")
	AShooterWeapon* GetCurrentWeapon() const { return CurrentWeapon; }

protected:

	/** Update ADS state and apply effects */
	void UpdateADS(float DeltaTime);

	/** Update HP regeneration based on movement speed */
	void UpdateRegeneration(float DeltaTime);

	/** Update first person view with recoil offsets */
	virtual void UpdateFirstPersonView(float DeltaTime) override;

	/** Called when melee attack hits something */
	UFUNCTION()
	void OnMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot);

	// ==================== SFX Functions ====================

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â® ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  */
	UFUNCTION(BlueprintCallable, Category = "SFX")
	void PlayFootstepSound();

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ */
	UFUNCTION(BlueprintCallable, Category = "SFX")
	void PlayCrouchFootstepSound();

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	void PlaySlideStartSound();

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	void PlaySlideEndSound();

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â¶ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â© ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	void StartSlideLoopSound();

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â¶ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â© ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	void StopSlideLoopSound();

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â  wallrun */
	void PlayWallRunStartSound();

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ wallrun */
	void PlayWallRunEndSound();

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â¶ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â© ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª wallrun */
	void StartWallRunLoopSound();

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â¶ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â© ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª wallrun */
	void StopWallRunLoopSound();

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â¦ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  */
	UFUNCTION(BlueprintCallable, Category = "SFX")
	void PlayJumpSound(bool bIsDoubleJump);

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ */
	void PlayLandSound(float FallSpeed);

	// ==================== SFX Delegate Handlers ====================

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UFUNCTION()
	void OnSlideStarted_SFX();

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UFUNCTION()
	void OnSlideEnded_SFX();

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â  wallrun */
	UFUNCTION()
	void OnWallRunStarted_SFX(EWallSide Side);

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ wallrun */
	UFUNCTION()
	void OnWallRunEnded_SFX();

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ */
	UFUNCTION()
	void OnLanded_SFX(const FHitResult& Hit);

	/** ÃƒÆ’Ã‚ÂÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¿ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¬ ApexMovementComponent */
	void BindMovementSFXDelegates();

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¿ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â² ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ ApexMovementComponent */
	void UnbindMovementSFXDelegates();

public:

	//~Begin IShooterWeaponHolder interface

	/** Attaches a weapon's meshes to the owner */
	virtual void AttachWeaponMeshes(AShooterWeapon* Weapon) override;

	/** Plays the firing montage for the weapon */
	virtual void PlayFiringMontage(UAnimMontage* Montage) override;

	/** Applies weapon recoil to the owner */
	virtual void AddWeaponRecoil(float Recoil) override;

	/** Updates the weapon's HUD with the current ammo count */
	virtual void UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize) override;

	/** Calculates and returns the aim location for the weapon */
	virtual FVector GetWeaponTargetLocation() override;

	/** Gives a weapon of this class to the owner */
	virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override;

	/** Activates the passed weapon */
	virtual void OnWeaponActivated(AShooterWeapon* Weapon) override;

	/** Deactivates the passed weapon */
	virtual void OnWeaponDeactivated(AShooterWeapon* Weapon) override;

	/** Notifies the owner that the weapon cooldown has expired and it's ready to shoot again */
	virtual void OnSemiWeaponRefire() override;

	/** Notifies the owner that a hit was registered */
	virtual void OnWeaponHit(const FVector& HitLocation, const FVector& HitDirection, float Damage, bool bHeadshot, bool bKilled) override;

	//~End IShooterWeaponHolder interface

protected:

	/** Returns true if the character already owns a weapon of the given class */
	AShooterWeapon* FindWeaponOfType(TSubclassOf<AShooterWeapon> WeaponClass) const;

	/** Called when this character's HP is depleted */
	void Die();

	/** Called to allow Blueprint code to react to this character's death */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter", meta = (DisplayName = "On Death"))
	void BP_OnDeath();

	/** Called from the respawn timer to destroy this character and force the PC to respawn */
	void OnRespawn();
};