// WeaponRecoilComponent.h
// Advanced procedural recoil system with patterns, recovery, visual kick and camera punch

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeaponRecoilComponent.generated.h"

class AShooterWeapon;
class UCameraShakeComponent;
class UCharacterMovementComponent;
class UApexMovementComponent;

/**
 * Recoil pattern point - defines pitch/yaw offset at a specific shot index
 */
USTRUCT(BlueprintType)
struct FRecoilPatternPoint
{
	GENERATED_BODY()

	/** Pitch offset (positive = up) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Pitch = 0.0f;

	/** Yaw offset (positive = right) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Yaw = 0.0f;

	FRecoilPatternPoint() {}
	FRecoilPatternPoint(float InPitch, float InYaw) : Pitch(InPitch), Yaw(InYaw) {}
};

/**
 * Complete recoil settings for a weapon
 */
USTRUCT(BlueprintType)
struct FWeaponRecoilSettings
{
	GENERATED_BODY()

	// ==================== Recoil Pattern ====================

	/** Base vertical recoil per shot (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float BaseVerticalRecoil = 0.8f;

	/** Base horizontal recoil per shot (degrees, random +/-) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float BaseHorizontalRecoil = 0.3f;

	/** Recoil pattern - if empty, uses base values with randomization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	TArray<FRecoilPatternPoint> RecoilPattern;

	/** How much random variation to add to pattern (0 = exact pattern, 1 = full random) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PatternRandomness = 0.2f;

	/** Recoil multiplier that increases with consecutive shots */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float ConsecutiveShotMultiplier = 1.15f;

	/** Maximum recoil multiplier from consecutive shots */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern", meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float MaxConsecutiveMultiplier = 2.5f;

	// ==================== Recoil Recovery ====================

	/** How fast camera returns to original position (degrees/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float RecoverySpeed = 15.0f;

	/** Delay before recovery starts (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RecoveryDelay = 0.1f;

	/** If true, player can manually pull down to counter recoil faster */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery")
	bool bAllowManualRecovery = true;

	// ==================== Visual Weapon Kick ====================

	/** Enable visual weapon kick (separate from camera) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick")
	bool bEnableVisualKick = true;

	/** Weapon kick back distance (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float KickBackDistance = 3.0f;

	/** Weapon kick up rotation (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick", meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float KickUpRotation = 4.0f;

	/** Weapon kick side rotation (degrees, random +/-) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float KickSideRotation = 1.0f;

	/** Visual kick return speed (higher = snappier) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick", meta = (ClampMin = "5.0", ClampMax = "50.0"))
	float KickReturnSpeed = 20.0f;

	/** Oscillation frequency for kick return */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick", meta = (ClampMin = "10.0", ClampMax = "40.0"))
	float KickOscillationFrequency = 25.0f;

	/** Damping for kick oscillation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick", meta = (ClampMin = "3.0", ClampMax = "15.0"))
	float KickOscillationDamping = 8.0f;

	// ==================== Camera Punch ====================

	/** Enable camera punch (micro-shake per shot) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Punch")
	bool bEnableCameraPunch = true;

	/** Camera punch intensity (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Punch", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float CameraPunchIntensity = 0.5f;

	/** Camera punch frequency (Hz) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Punch", meta = (ClampMin = "15.0", ClampMax = "50.0"))
	float CameraPunchFrequency = 30.0f;

	/** Camera punch damping */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Punch", meta = (ClampMin = "5.0", ClampMax = "20.0"))
	float CameraPunchDamping = 12.0f;

	// ==================== Weapon Sway ====================

	/** Enable procedural weapon sway */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Sway")
	bool bEnableWeaponSway = true;

	/** Mouse movement sway intensity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Sway", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float MouseSwayIntensity = 1.5f;

	/** Mouse sway lag (lower = more responsive) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Sway", meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float MouseSwayLag = 8.0f;

	/** Max mouse sway offset (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Sway", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float MaxMouseSwayOffset = 3.0f;

	/** Idle breathing sway intensity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Sway", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float BreathingSwayIntensity = 0.3f;

	/** Breathing frequency (Hz) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Sway", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float BreathingFrequency = 0.25f;

	/** Movement sway intensity multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Sway", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float MovementSwayMultiplier = 1.0f;

	// ==================== Situational Multipliers ====================

	/** Recoil multiplier when in air */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multipliers", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float AirborneRecoilMultiplier = 1.5f;

	/** Recoil multiplier when crouching */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multipliers", meta = (ClampMin = "0.3", ClampMax = "1.0"))
	float CrouchRecoilMultiplier = 0.7f;

	/** Recoil multiplier when aiming down sights */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multipliers", meta = (ClampMin = "0.3", ClampMax = "1.0"))
	float ADSRecoilMultiplier = 0.6f;

	/** Recoil multiplier when moving */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multipliers", meta = (ClampMin = "0.8", ClampMax = "2.0"))
	float MovingRecoilMultiplier = 1.2f;
};

/**
 * Advanced weapon recoil component with:
 * - Learnable recoil patterns
 * - Recoil recovery with manual pull-down
 * - Visual weapon kick with damped oscillation
 * - Camera punch (micro-shake)
 * - Procedural weapon sway (mouse lag, breathing, movement)
 */
UCLASS(ClassGroup = (Weapon), meta = (BlueprintSpawnableComponent))
class POLARITY_API UWeaponRecoilComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponRecoilComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==================== Setup ====================

	/** Initialize with references */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void Initialize(APlayerController* InController, UCharacterMovementComponent* InMovement, UApexMovementComponent* InApexMovement = nullptr);

	/** Set recoil settings (usually from weapon) */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void SetRecoilSettings(const FWeaponRecoilSettings& InSettings);

	// ==================== Firing Events ====================

	/** Called when weapon fires - triggers recoil */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void OnWeaponFired();

	/** Called when firing stops - reset consecutive shot counter */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void OnFiringEnded();

	/** Reset all recoil state */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void ResetRecoil();

	// ==================== Input ====================

	/** Feed mouse input for sway calculation */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void AddMouseInput(float DeltaYaw, float DeltaPitch);

	// ==================== Getters ====================

	/** Get current visual weapon offset */
	UFUNCTION(BlueprintPure, Category = "Recoil")
	FVector GetWeaponOffset() const { return CurrentWeaponOffset; }

	/** Get current visual weapon rotation offset */
	UFUNCTION(BlueprintPure, Category = "Recoil")
	FRotator GetWeaponRotationOffset() const { return CurrentWeaponRotation; }

	/** Get camera rotation offset from punch effect */
	UFUNCTION(BlueprintPure, Category = "Recoil")
	FRotator GetCameraPunchOffset() const { return CurrentCameraPunch; }

	/** Check if currently recovering from recoil */
	UFUNCTION(BlueprintPure, Category = "Recoil")
	bool IsRecovering() const { return bIsRecovering; }

	// ==================== State Setters ====================

	/** Set ADS state for recoil reduction */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void SetAiming(bool bAiming) { bIsAiming = bAiming; }

	/** Set crouching state for recoil reduction */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void SetCrouching(bool bCrouching) { bIsCrouching = bCrouching; }

protected:
	// ==================== Settings ====================

	UPROPERTY()
	FWeaponRecoilSettings Settings;

	// ==================== References ====================

	UPROPERTY()
	TObjectPtr<APlayerController> OwnerController;

	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> MovementComponent;

	UPROPERTY()
	TObjectPtr<UApexMovementComponent> ApexMovement;

	// ==================== Recoil State ====================

	/** Current shot index in pattern */
	int32 CurrentShotIndex = 0;

	/** Current consecutive shot multiplier */
	float CurrentConsecutiveMultiplier = 1.0f;

	/** Total accumulated recoil (for recovery) */
	FRotator AccumulatedRecoil = FRotator::ZeroRotator;

	/** Time since last shot (for recovery delay) */
	float TimeSinceLastShot = 0.0f;

	/** Is recoil recovery active */
	bool bIsRecovering = false;

	/** Is currently firing (for consecutive shots) */
	bool bIsFiring = false;

	// ==================== Visual Kick State ====================

	/** Current weapon position offset */
	FVector CurrentWeaponOffset = FVector::ZeroVector;

	/** Current weapon rotation offset */
	FRotator CurrentWeaponRotation = FRotator::ZeroRotator;

	/** Target weapon position (from kick) */
	FVector TargetWeaponOffset = FVector::ZeroVector;

	/** Target weapon rotation (from kick) */
	FRotator TargetWeaponRotation = FRotator::ZeroRotator;

	/** Kick oscillation time */
	float KickOscillationTime = 0.0f;

	/** Kick oscillation amplitude */
	float KickOscillationAmplitude = 0.0f;

	// ==================== Camera Punch State ====================

	/** Current camera punch rotation */
	FRotator CurrentCameraPunch = FRotator::ZeroRotator;

	/** Punch oscillation time */
	float PunchOscillationTime = 0.0f;

	/** Punch oscillation amplitude */
	FVector2D PunchOscillationAmplitude = FVector2D::ZeroVector;

	// ==================== Weapon Sway State ====================

	/** Current mouse velocity (for sway) */
	FVector2D CurrentMouseVelocity = FVector2D::ZeroVector;

	/** Smoothed mouse velocity */
	FVector2D SmoothedMouseVelocity = FVector2D::ZeroVector;

	/** Current sway offset */
	FRotator CurrentSwayOffset = FRotator::ZeroRotator;

	/** Breathing time accumulator */
	float BreathingTime = 0.0f;

	// ==================== Character State ====================

	bool bIsAiming = false;
	bool bIsCrouching = false;

	// ==================== Internal Methods ====================

	/** Calculate recoil for current shot */
	FRotator CalculateShotRecoil();

	/** Get situational recoil multiplier */
	float GetSituationalMultiplier() const;

	/** Check if character is airborne */
	bool IsAirborne() const;

	/** Check if character is moving */
	bool IsMoving() const;

	/** Apply recoil to controller */
	void ApplyRecoilToController(const FRotator& Recoil);

	/** Update recoil recovery */
	void UpdateRecovery(float DeltaTime);

	/** Update visual weapon kick */
	void UpdateVisualKick(float DeltaTime);

	/** Update camera punch */
	void UpdateCameraPunch(float DeltaTime);

	/** Update weapon sway */
	void UpdateWeaponSway(float DeltaTime);

	/** Trigger visual kick effect */
	void TriggerVisualKick();

	/** Trigger camera punch effect */
	void TriggerCameraPunch();
};
