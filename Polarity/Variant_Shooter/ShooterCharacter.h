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
class UCameraShakeBase;
class UMaterialInterface;
class UIKRetargeter;
class UNiagaraSystem;
class UNiagaraComponent;
struct FCheckpointData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBulletCountUpdatedDelegate, int32, MagazineSize, int32, Bullets);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDamagedDelegate, float, LifePercent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDamageDirectionDelegate, float, AngleDegrees, float, Damage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHeatUpdatedDelegate, float, HeatPercent, float, DamageMultiplier);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSpeedUpdatedDelegate, float, SpeedPercent, float, CurrentSpeed, float, MaxSpeed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPolarityChangedDelegate, uint8, NewPolarity, float, ChargeValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FChargeUpdatedDelegate, float, ChargeValue, uint8, Polarity);
// Extended charge delegate: TotalCharge, StableCharge, UnstableCharge, MaxStable, MaxUnstable, Polarity
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SixParams(FChargeExtendedDelegate, float, TotalCharge, float, StableCharge, float, UnstableCharge, float, MaxStableCharge, float, MaxUnstableCharge, uint8, Polarity);
// Chromatic aberration intensity delegate (called every tick while effect is active)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDamageChromaticAberrationDelegate, float, Intensity);

// Boss Finisher delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossFinisherStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossFinisherEnded);

/**
 * Settings for Boss Finisher cinematic attack
 * All times are in seconds, distances in cm
 */
USTRUCT(BlueprintType)
struct FBossFinisherSettings
{
	GENERATED_BODY()

	/** Target point in world coordinates (set from Level BP) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	FVector TargetPoint = FVector::ZeroVector;

	/** Total time to travel from current position to target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float TotalTravelTime = 2.0f;

	/** Time before arrival to straighten trajectory (switch from curve to linear) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float StraightenTime = 0.5f;

	/** Time before arrival to start melee animation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float AnimationStartTime = 0.3f;

	/** Time to hang in the air after reaching target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float HangTime = 0.5f;

	/** Approach offset relative to target - defines "from which side" to approach
	 *  Example: (500, 0, 200) means approach from 500cm in front and 200cm above target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	FVector ApproachOffset = FVector(500.0f, 0.0f, 200.0f);
};

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

	// ==================== UE4 Mesh System (Copy Pose from Mesh) ====================

	/** UE4 First Person Mesh (visible, copies pose from FirstPersonMesh via IK Retargeter) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|UE4 Meshes", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> UE4_FPMesh;

	/** UE4 Melee Mesh (visible, copies pose from MeleeMesh via IK Retargeter) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|UE4 Meshes", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> UE4_MeleeMesh;

protected:

	/** Fire weapon input action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* FireAction;

	/** Switch weapon input action (cycles through weapons) */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SwitchWeaponAction;

	/** Map of input actions to specific weapon classes (direct weapon hotkeys) */
	UPROPERTY(EditAnywhere, Category = "Input|Weapon Hotkeys")
	TMap<UInputAction*, TSubclassOf<AShooterWeapon>> WeaponHotkeys;

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

	// ==================== Left Hand IK ====================

	/** Socket name on weapon mesh for left hand grip */
	UPROPERTY(EditAnywhere, Category = "Weapons|Left Hand IK")
	FName LeftHandGripSocket = FName("GripPoint_002");

	/** Offset applied to the left hand IK target relative to the socket */
	UPROPERTY(EditAnywhere, Category = "Weapons|Left Hand IK")
	FTransform LeftHandIKOffset = FTransform::Identity;

	/** Current left hand IK alpha (0 = no IK/detached, 1 = full IK). Blend this for wallrun etc. */
	float CurrentLeftHandIKAlpha = 1.0f;

	/** Target left hand IK alpha to interpolate towards */
	float TargetLeftHandIKAlpha = 1.0f;

	/** Interpolation speed for left hand IK alpha */
	UPROPERTY(EditAnywhere, Category = "Weapons|Left Hand IK")
	float LeftHandIKAlphaInterpSpeed = 10.0f;

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

	// ==================== Weapon Switch Animation ====================

	/** True while weapon switch animation is in progress */
	bool bIsWeaponSwitchInProgress = false;

	/** Weapon to switch to after lowering animation completes */
	UPROPERTY()
	TObjectPtr<AShooterWeapon> PendingWeapon = nullptr;

	/** Progress of weapon switch mesh transition (0-1) */
	float WeaponSwitchProgress = 0.0f;

	/** Base location of FirstPersonMesh for weapon switch (stored at switch start) */
	FVector WeaponSwitchMeshBaseLocation = FVector::ZeroVector;

	/** True during lowering phase, false during raising phase */
	bool bIsWeaponLowering = true;

	/** Time to lower weapon mesh during switch */
	UPROPERTY(EditAnywhere, Category = "Weapons|Switch Animation", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float WeaponSwitchLowerTime = 0.15f;

	/** Time to raise weapon mesh during switch */
	UPROPERTY(EditAnywhere, Category = "Weapons|Switch Animation", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float WeaponSwitchRaiseTime = 0.15f;

	/** Time before respawn after death */
	UPROPERTY(EditAnywhere, Category = "Death", meta = (ClampMin = 0, ClampMax = 10, Units = "s"))
	float RespawnTime = 2.0f;

	/** Duration of fade to black on death */
	UPROPERTY(EditAnywhere, Category = "Death", meta = (ClampMin = 0.1, ClampMax = 3.0, Units = "s"))
	float DeathFadeOutDuration = 0.5f;

	/** Duration of fade from black on respawn */
	UPROPERTY(EditAnywhere, Category = "Death", meta = (ClampMin = 0.1, ClampMax = 3.0, Units = "s"))
	float RespawnFadeInDuration = 0.3f;

	/** Color of death/respawn fade */
	UPROPERTY(EditAnywhere, Category = "Death")
	FLinearColor DeathFadeColor = FLinearColor::Black;

	FTimerHandle RespawnTimer;

	// ==================== UE4 Mesh System Settings ====================

	/** Enable UE4 mesh system (uses Copy Pose from Mesh with IK Retargeting) */
	UPROPERTY(EditDefaultsOnly, Category = "UE4 Meshes")
	bool bUseUE4Meshes = false;

	/** IK Retargeter for First Person Mesh (UE5 Mannequin -> UE4 Skeleton) */
	UPROPERTY(EditDefaultsOnly, Category = "UE4 Meshes", meta = (EditCondition = "bUseUE4Meshes"))
	TObjectPtr<UIKRetargeter> FPMeshRetargeter;

	/** IK Retargeter for Melee Mesh (UE5 Mannequin -> UE4 Skeleton) */
	UPROPERTY(EditDefaultsOnly, Category = "UE4 Meshes", meta = (EditCondition = "bUseUE4Meshes"))
	TObjectPtr<UIKRetargeter> MeleeMeshRetargeter;

	// ==================== UI Speed & Polarity ====================

	/** Maximum speed for UI normalization (cm/s) */
	UPROPERTY(EditAnywhere, Category = "UI", meta = (ClampMin = "100.0"))
	float MaxSpeedForUI = 1200.0f;

	/** Previous polarity state for change detection (0=Neutral, 1=Positive, 2=Negative) */
	uint8 PreviousPolarity = 0;

	// ==================== Charge Stability Thresholds ====================

	/** Charge threshold below which state is considered Stable (0-1 absolute) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Charge", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ChargeStableThreshold = 0.3f;

	/** Charge threshold above which state is considered Unstable (0-1 absolute) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Charge", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ChargeUnstableThreshold = 0.7f;

	// ==================== Charge Overlay Materials ====================

	/** If true, overlay material will be applied based on charge state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay")
	bool bUseChargeOverlay = false;

	/** Overlay material to apply when charge is neutral (near zero) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay", meta = (EditCondition = "bUseChargeOverlay"))
	TObjectPtr<UMaterialInterface> NeutralChargeOverlayMaterial;

	/** Overlay material to apply when charge is positive */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay", meta = (EditCondition = "bUseChargeOverlay"))
	TObjectPtr<UMaterialInterface> PositiveChargeOverlayMaterial;

	/** Overlay material to apply when charge is negative */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay", meta = (EditCondition = "bUseChargeOverlay"))
	TObjectPtr<UMaterialInterface> NegativeChargeOverlayMaterial;

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

	// ==================== SFX|Air Dash ====================

	/** Sound played when performing an air dash */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Air Dash")
	TObjectPtr<USoundBase> AirDashSound;

	/** Minimum pitch for air dash sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Air Dash", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float AirDashSoundPitchMin = 0.95f;

	/** Maximum pitch for air dash sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Air Dash", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float AirDashSoundPitchMax = 1.05f;

	/** Volume for air dash sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Air Dash", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float AirDashSoundVolume = 1.0f;

	// ==================== SFX|Mantle ====================

	/** Sound played when mantling */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Mantle")
	TObjectPtr<USoundBase> MantleSound;

	/** Volume for mantle sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Mantle", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MantleSoundVolume = 1.0f;

	// ==================== SFX|Weapon ====================

	/** Sound played when switching weapons */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Weapon")
	TObjectPtr<USoundBase> WeaponSwitchSound;

	/** Volume for weapon switch sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Weapon", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float WeaponSwitchSoundVolume = 0.8f;

	// ==================== SFX|Low Health ====================

	/** Warning sound played when health is critically low */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Low Health")
	TObjectPtr<USoundBase> LowHealthWarningSound;

	/** HP threshold (0-1) below which warning plays */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Low Health", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowHealthThreshold = 0.25f;

	/** Interval between warning sounds (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Low Health", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float LowHealthWarningInterval = 2.0f;

	/** Volume for low health warning sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Low Health", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float LowHealthWarningVolume = 0.7f;

	/** Is currently in low health state */
	bool bIsLowHealth = false;

	/** Timer for low health warning sounds */
	float LowHealthWarningTimer = 0.0f;

	// ==================== Damage Feedback ====================

	/** Camera shake to play when taking damage. Intensity is scaled by DamageToCameraShakeCurve */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Camera Shake")
	TSubclassOf<UCameraShakeBase> DamageCameraShake;

	/** Curve mapping damage amount to camera shake intensity (X = damage, Y = intensity multiplier 0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Camera Shake")
	TObjectPtr<UCurveFloat> DamageToCameraShakeCurve;

	/** Maximum camera shake scale when curve returns 1.0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Camera Shake", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float MaxCameraShakeScale = 2.0f;

	// --- Impact Sounds by Damage Type ---

	/** Default impact sound when damage type is unknown */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Impact Sounds")
	TObjectPtr<USoundBase> DefaultImpactSound;

	/** Impact sound for ranged/bullet damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Impact Sounds")
	TObjectPtr<USoundBase> RangedImpactSound;

	/** Impact sound for melee damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Impact Sounds")
	TObjectPtr<USoundBase> MeleeImpactSound;

	/** Impact sound for explosion damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Impact Sounds")
	TObjectPtr<USoundBase> ExplosionImpactSound;

	/** Impact sound for EMF/electric damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Impact Sounds")
	TObjectPtr<USoundBase> EMFImpactSound;

	/** Volume multiplier for damage impact sounds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Impact Sounds", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float DamageImpactSoundVolume = 1.0f;

	// --- Chromatic Aberration ---

	/** Damage amount that results in maximum (1.0) chromatic aberration intensity. Higher damage is clamped to 1.0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Chromatic Aberration", meta = (ClampMin = "1.0"))
	float MaxDamageForFullChromaticAberration = 100.0f;

	/** Duration of chromatic aberration effect (half sine wave: 0 → peak → 0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Chromatic Aberration", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float ChromaticAberrationDuration = 0.5f;

	/** Chromatic aberration delegate - broadcasts intensity (0-1) every tick while effect is active */
	UPROPERTY(BlueprintAssignable, Category = "Damage Feedback|Chromatic Aberration")
	FDamageChromaticAberrationDelegate OnDamageChromaticAberration;

	// --- Chromatic Aberration State (internal) ---

	/** Base intensity calculated from damage (before sine modulation) */
	float ChromaticAberrationBaseIntensity = 0.0f;

	/** Elapsed time for current chromatic aberration effect */
	float ChromaticAberrationElapsedTime = 0.0f;

	/** True when chromatic aberration effect is active */
	bool bChromaticAberrationActive = false;

	// --- Melee Knockback (position interpolation, like NPC system) ---

	/** Whether to apply knockback when hit by melee attacks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Knockback")
	bool bEnableMeleeKnockback = true;

	/** Distance to knock back player on melee hit (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Knockback", meta = (ClampMin = "0.0", EditCondition = "bEnableMeleeKnockback"))
	float MeleeKnockbackDistance = 200.0f;

	/** Duration of knockback interpolation (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Knockback", meta = (ClampMin = "0.05", ClampMax = "1.0", EditCondition = "bEnableMeleeKnockback"))
	float MeleeKnockbackDuration = 0.2f;

	/** If true, knockback can be cancelled by player actions (jump, dash, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Knockback", meta = (EditCondition = "bEnableMeleeKnockback"))
	bool bKnockbackCancellableByPlayer = true;

	// --- Knockback State (internal) ---

	/** True when player is being knocked back */
	bool bIsInKnockback = false;

	/** Start position for knockback interpolation */
	FVector KnockbackStartPosition = FVector::ZeroVector;

	/** Target position for knockback interpolation */
	FVector KnockbackTargetPosition = FVector::ZeroVector;

	/** Total duration of current knockback */
	float KnockbackTotalDuration = 0.0f;

	/** Elapsed time in current knockback */
	float KnockbackElapsedTime = 0.0f;

	// ==================== VFX|PostProcess ====================

	/** Post process material instance for low health effect (vignette, desaturation, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|PostProcess")
	TObjectPtr<UMaterialInstanceDynamic> LowHealthPPMaterial;

	/** Post process material instance for high speed effect (motion blur, chromatic aberration, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|PostProcess")
	TObjectPtr<UMaterialInstanceDynamic> HighSpeedPPMaterial;

	/** Parameter name for intensity in post process materials */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|PostProcess")
	FName PPIntensityParameterName = FName("Intensity");

	/** Speed threshold at which high speed effect starts (units/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|PostProcess", meta = (ClampMin = "0.0"))
	float HighSpeedThreshold = 1500.0f;

	/** Speed at which high speed effect reaches maximum intensity (units/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|PostProcess", meta = (ClampMin = "0.0"))
	float HighSpeedMaxThreshold = 3000.0f;

	/** Interpolation speed for post process effects (higher = snappier) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|PostProcess", meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float PPInterpSpeed = 5.0f;

	/** Current low health PP intensity (internal) */
	float CurrentLowHealthPPIntensity = 0.0f;

	/** Current high speed PP intensity (internal) */
	float CurrentHighSpeedPPIntensity = 0.0f;

	// ==================== VFX|Movement ====================

	/** Niagara system for air dash trail effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Movement")
	TObjectPtr<UNiagaraSystem> AirDashTrailFX;

	/** Niagara system for double jump burst effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Movement")
	TObjectPtr<UNiagaraSystem> DoubleJumpFX;

	/** Scale for double jump VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Movement", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float DoubleJumpFXScale = 1.0f;

	/** Active air dash trail component */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveAirDashTrailComponent;

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

	/** Extended charge delegate with stable/unstable breakdown (fires every tick) */
	FChargeExtendedDelegate OnChargeExtended;

public:

	/** Constructor */
	AShooterCharacter();

	/** Get the melee mesh component (can return nullptr if not set up) */
	UFUNCTION(BlueprintPure, Category = "Character")
	USkeletalMeshComponent* GetMeleeMesh() const;

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

	/** Returns true if player is dead (HP <= 0) */
	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return CurrentHP <= 0.0f; }

	/** Returns true if player is currently being knocked back */
	UFUNCTION(BlueprintPure, Category = "Damage")
	bool IsInKnockback() const { return bIsInKnockback; }

	/** Cancel current knockback (called when player performs action like jump/dash) */
	UFUNCTION(BlueprintCallable, Category = "Damage")
	void CancelKnockback();

protected:

	/** Apply knockback from melee damage */
	void ApplyMeleeKnockback(const FVector& KnockbackDirection, float Distance, float Duration);

	/** Update knockback position interpolation (called from Tick) */
	void UpdateKnockbackInterpolation(float DeltaTime);

	/** Play damage feedback effects (camera shake, sound) based on damage type and amount */
	void PlayDamageFeedback(float Damage, TSubclassOf<UDamageType> DamageTypeClass);

	/** Get impact sound for damage type */
	USoundBase* GetImpactSoundForDamageType(TSubclassOf<UDamageType> DamageTypeClass) const;

	/** Start chromatic aberration effect based on damage amount */
	void StartChromaticAberrationEffect(float Damage);

	/** Update chromatic aberration effect (called from Tick) */
	void UpdateChromaticAberration(float DeltaTime);

public:

	/** Handles start firing input */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStartFiring();

	/** Handles stop firing input */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStopFiring();

	/** Handles switch weapon input (cycles through weapons) */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoSwitchWeapon();

	/** Handles weapon hotkey input - switches to specific weapon class if owned */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoWeaponHotkey(TSubclassOf<AShooterWeapon> WeaponClass);

	/** Returns true if weapon switch is currently in progress */
	UFUNCTION(BlueprintPure, Category = "Weapons")
	bool IsWeaponSwitchInProgress() const { return bIsWeaponSwitchInProgress; }

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

	/** Sets the target left hand IK alpha (0 = detached, 1 = fully attached). Use for wallrun, melee, etc. */
	UFUNCTION(BlueprintCallable, Category = "Weapons|Left Hand IK")
	void SetLeftHandIKAlpha(float Alpha) { TargetLeftHandIKAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f); }

	/** Gets the current left hand IK alpha */
	UFUNCTION(BlueprintPure, Category = "Weapons|Left Hand IK")
	float GetLeftHandIKAlpha() const { return CurrentLeftHandIKAlpha; }

protected:

	/** Update ADS state and apply effects */
	void UpdateADS(float DeltaTime);

	/** Start weapon switch to specified weapon (with lowering/raising animation) */
	void StartWeaponSwitch(AShooterWeapon* NewWeapon);

	/** Update weapon switch animation */
	void UpdateWeaponSwitch(float DeltaTime);

	/** Called when weapon lowering completes - performs actual weapon swap */
	void OnWeaponSwitchLowered();

	/** Called when weapon raising completes - ends switch process */
	void OnWeaponSwitchRaised();

	/** Updates first person mesh visibility based on weapon ownership */
	void UpdateFirstPersonMeshVisibility();

	/** Checks if a bone is a child of (or is) any of the specified root bones */
	/** Update HP regeneration based on movement speed */
	void UpdateRegeneration(float DeltaTime);

	/** Update overlay material based on current charge polarity */
	void UpdateChargeOverlay(uint8 NewPolarity);

	/** Update first person view with recoil offsets */
	virtual void UpdateFirstPersonView(float DeltaTime) override;

	/** Called when melee attack hits something */
	UFUNCTION()
	void OnMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage);

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

	// ==================== New Movement SFX/VFX Handlers ====================

	/** Handler for jump events from ApexMovementComponent */
	UFUNCTION()
	void OnJumpPerformed_Handler(bool bIsDoubleJump);

	/** Handler for mantle start event */
	UFUNCTION()
	void OnMantleStarted_Handler();

	/** Handler for air dash start event - plays sound and starts VFX */
	UFUNCTION()
	void OnAirDashStarted_Handler();

	/** Handler for air dash end event - stops VFX */
	UFUNCTION()
	void OnAirDashEnded_Handler();

	/** Play air dash sound */
	void PlayAirDashSound();

	/** Play mantle sound */
	void PlayMantleSound();

	/** Play weapon switch sound */
	void PlayWeaponSwitchSound();

	/** Update low health warning state and play warning sounds */
	void UpdateLowHealthWarning(float DeltaTime);

	/** Update post process effects based on health and speed */
	void UpdatePostProcessEffects(float DeltaTime);

	/** Spawn double jump VFX at character feet */
	void SpawnDoubleJumpVFX();

	/** Start air dash trail VFX attached to character */
	void StartAirDashTrailVFX();

	/** Stop air dash trail VFX */
	void StopAirDashTrailVFX();

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

	/** Updates left hand IK transform from weapon socket and passes it to AnimInstance */
	void UpdateLeftHandIK(float DeltaTime);

	/** Sets the left hand IK transform and alpha in the AnimInstance via reflection */
	void SetAnimInstanceLeftHandIK(const FTransform& Transform, float Alpha);

	/** Returns true if the character already owns a weapon of the given class */
	AShooterWeapon* FindWeaponOfType(TSubclassOf<AShooterWeapon> WeaponClass) const;

	// ==================== Checkpoint System ====================
public:
	/**
	 * Save current character state to checkpoint data.
	 * Called by CheckpointSubsystem when checkpoint is activated.
	 * @param OutData Structure to fill with character state
	 * @return True if save was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	bool SaveToCheckpoint(UPARAM(ref) FCheckpointData& OutData);

	/**
	 * Restore character state from checkpoint data.
	 * Called by CheckpointSubsystem on respawn.
	 * @param Data Checkpoint data to restore from
	 * @return True if restore was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	bool RestoreFromCheckpoint(const FCheckpointData& Data);

	/** Blueprint event called after successful respawn at checkpoint */
	UFUNCTION(BlueprintImplementableEvent, Category = "Checkpoint", meta = (DisplayName = "On Respawn At Checkpoint"))
	void BP_OnRespawnAtCheckpoint();

protected:
	/**
	 * Reset character state to defaults (velocity, cooldowns, etc.)
	 * Called during respawn before restoring checkpoint data.
	 */
	void ResetCharacterState();

	/** Called when this character's HP is depleted */
	void Die();

	/** Called to allow Blueprint code to react to this character's death */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter", meta = (DisplayName = "On Death"))
	void BP_OnDeath();

	/** Called from the respawn timer to destroy this character and force the PC to respawn */
	void OnRespawn();

	// ==================== Boss Finisher System ====================
public:
	/** Flag indicating boss finisher mode is active - set from Level BP */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss Finisher")
	bool bIsOnBossFinisher = false;

	/** Settings for the boss finisher - configure from Level BP */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss Finisher")
	FBossFinisherSettings BossFinisherSettings;

	/** Called when boss finisher starts */
	UPROPERTY(BlueprintAssignable, Category = "Boss Finisher")
	FOnBossFinisherStarted OnBossFinisherStarted;

	/** Called when boss finisher ends */
	UPROPERTY(BlueprintAssignable, Category = "Boss Finisher")
	FOnBossFinisherEnded OnBossFinisherEnded;

	/**
	 * Start the boss finisher sequence.
	 * Call this from Level BP after setting bIsOnBossFinisher and BossFinisherSettings.
	 * Triggered by melee input when bIsOnBossFinisher is true.
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss Finisher")
	void StartBossFinisher();

	/**
	 * Abort the boss finisher and return to normal state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss Finisher")
	void StopBossFinisher();

	/** Check if boss finisher is currently executing */
	UFUNCTION(BlueprintPure, Category = "Boss Finisher")
	bool IsBossFinisherActive() const { return bBossFinisherActive; }

protected:
	// ==================== Boss Finisher State ====================

	/** True while finisher sequence is executing */
	bool bBossFinisherActive = false;

	/** Current phase of boss finisher */
	enum class EBossFinisherPhase : uint8
	{
		None,
		CurveMovement,		// Moving along Bezier curve
		LinearMovement,		// Moving in straight line to target
		Animation,			// Playing attack animation while moving
		Hanging,			// Suspended at target point
		Falling				// Falling down with gravity
	};
	EBossFinisherPhase BossFinisherPhase = EBossFinisherPhase::None;

	/** Time elapsed since finisher started */
	float BossFinisherElapsedTime = 0.0f;

	/** Player position when finisher started */
	FVector BossFinisherStartPosition = FVector::ZeroVector;

	/** Bezier curve control points (P0=Start, P1, P2, P3=Target) */
	FVector BezierP0, BezierP1, BezierP2, BezierP3;

	/** Position when linear movement phase started */
	FVector LinearStartPosition = FVector::ZeroVector;

	/** Time when linear phase started */
	float LinearStartTime = 0.0f;

	/** Update boss finisher movement and state */
	void UpdateBossFinisher(float DeltaTime);

	/** Calculate position on cubic Bezier curve at time t (0-1) */
	FVector EvaluateBezierCurve(float T) const;

	/** Setup Bezier control points based on start position and settings */
	void SetupBezierCurve();

	/** Start the melee animation for boss finisher (uses Air Attack settings) */
	void StartBossFinisherAnimation();

	/** End boss finisher and restore normal state */
	void EndBossFinisher();
};