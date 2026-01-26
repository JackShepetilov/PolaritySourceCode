// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShooterWeaponHolder.h"
#include "Animation/AnimInstance.h"
#include "WeaponRecoilComponent.h"
#include "ShooterWeapon.generated.h"

class IShooterWeaponHolder;
class AShooterProjectile;
class USkeletalMeshComponent;
class UCameraComponent;
class UAnimMontage;
class UAnimInstance;
class UNiagaraSystem;
class UNiagaraComponent;
class UPhysicalMaterial;
class UDamageType;
class UCharacterMovementComponent;
class USoundAttenuation;

// Delegate for heat updates (for UI binding)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHeatChanged, float, NewHeat);

// Delegate called when weapon fires a shot (for NPC burst counting)
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeaponShotFired);

/**
 *  Base class for a first person shooter weapon
 *
 *  NEW SYSTEMS:
 *  - Heat System: Weapon heats up when firing, cools down faster with movement
 *  - Z-Factor: Bonus damage when shooting from above (rewards using EMF to gain height)
 */
UCLASS(abstract)
class POLARITY_API AShooterWeapon : public AActor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* ThirdPersonMesh;

	/** Camera component for ADS view - attached to weapon sight */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* ADSCameraComponent;

protected:

	IShooterWeaponHolder* WeaponOwner;

	// ==================== Firing Mode ====================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Firing")
	bool bUseHitscan = false;

	// ==================== Charge-Based Firing ====================

	/** If true, weapon consumes charge from owner to fire projectiles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Firing|Charge")
	bool bUseChargeFiring = false;

	/** Charge cost per shot (taken from owner's EMF charge) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Firing|Charge", meta = (EditCondition = "bUseChargeFiring", ClampMin = "0.0"))
	float ChargePerShot = 3.0f;

	/** Minimum charge module allowed (can still fire weak shots below this) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Firing|Charge", meta = (EditCondition = "bUseChargeFiring", ClampMin = "0.0"))
	float MinimumBaseCharge = 10.0f;

	/** If true, prevent firing when charge is below minimum (otherwise fires weakened shot) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Firing|Charge", meta = (EditCondition = "bUseChargeFiring"))
	bool bBlockFiringBelowMinimum = false;

	// ==================== Projectile Settings ====================

	UPROPERTY(EditAnywhere, Category = "Projectile", meta = (EditCondition = "!bUseHitscan"))
	TSubclassOf<AShooterProjectile> ProjectileClass;

	// ==================== Hitscan Settings ====================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitscan", meta = (EditCondition = "bUseHitscan", ClampMin = "0"))
	float HitscanDamage = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitscan", meta = (EditCondition = "bUseHitscan", ClampMin = "0"))
	float MaxHitscanRange = 10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitscan", meta = (EditCondition = "bUseHitscan", ClampMin = "1.0"))
	float HeadshotMultiplier = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitscan", meta = (EditCondition = "bUseHitscan"))
	TSubclassOf<UDamageType> HitscanDamageType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitscan", meta = (EditCondition = "bUseHitscan", ClampMin = "0"))
	float HitscanPhysicsForce = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitscan", meta = (EditCondition = "bUseHitscan"))
	bool bHitscanDamageOwner = false;

	// ==================== Heat System ====================

	/** Enable heat system - weapon heats up when firing, damage decreases with heat */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat System")
	bool bUseHeatSystem = true;

	/** Heat added per shot (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat System", meta = (EditCondition = "bUseHeatSystem", ClampMin = "0.0", ClampMax = "0.5"))
	float HeatPerShot = 0.08f;

	/** Base heat decay rate (units per second) when stationary */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat System", meta = (EditCondition = "bUseHeatSystem", ClampMin = "0.0", ClampMax = "2.0"))
	float BaseHeatDecayRate = 0.15f;

	/** Additional decay multiplier from movement speed. At max speed: decay = Base * (1 + Bonus) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat System", meta = (EditCondition = "bUseHeatSystem", ClampMin = "0.0", ClampMax = "5.0"))
	float SpeedHeatDecayBonus = 2.0f;

	/** Speed considered "maximum" for heat decay bonus (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat System", meta = (EditCondition = "bUseHeatSystem", ClampMin = "100.0"))
	float MaxSpeedForHeatBonus = 1200.0f;

	/** Minimum damage multiplier at maximum heat (0.2 = 20% damage) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat System", meta = (EditCondition = "bUseHeatSystem", ClampMin = "0.1", ClampMax = "1.0"))
	float MinHeatDamageMultiplier = 0.2f;

	/** Current heat level (0-1), read-only in BP */
	UPROPERTY(BlueprintReadOnly, Category = "Heat System")
	float CurrentHeat = 0.0f;

	// ==================== Z-Factor (Height Advantage) ====================

	/** Enable Z-Factor system - bonus damage when shooting from above */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Z-Factor")
	bool bUseZFactor = true;

	/** Maximum damage multiplier when shooting from above (1.5 = +50% damage) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Z-Factor", meta = (EditCondition = "bUseZFactor", ClampMin = "1.0", ClampMax = "3.0"))
	float ZFactorMaxMultiplier = 1.5f;

	/** Height difference for maximum bonus (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Z-Factor", meta = (EditCondition = "bUseZFactor", ClampMin = "100.0", ClampMax = "2000.0"))
	float ZFactorMaxHeightDiff = 500.0f;

	/** Minimum height difference to start bonus (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Z-Factor", meta = (EditCondition = "bUseZFactor", ClampMin = "0.0", ClampMax = "500.0"))
	float ZFactorMinHeightDiff = 50.0f;

	// ==================== Wave Divergence ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Divergence", meta = (EditCondition = "bUseHitscan", ClampMin = "0.0", ClampMax = "1.0"))
	float WaveDivergence = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Divergence", meta = (EditCondition = "bUseHitscan", ClampMin = "0.0", ClampMax = "1.0"))
	float MinDamageMultiplier = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Divergence", meta = (EditCondition = "bUseHitscan", ClampMin = "0.1", ClampMax = "30.0"))
	float MaxDivergenceAngle = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Divergence", meta = (EditCondition = "bUseHitscan", ClampMin = "0.0"))
	float InitialWaveRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Divergence", meta = (EditCondition = "bUseHitscan", ClampMin = "10.0"))
	float TargetEffectiveRadius = 50.0f;

	// ==================== Reflection ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Reflection", meta = (EditCondition = "bUseHitscan", ClampMin = "0", ClampMax = "5"))
	int32 MaxReflections = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Reflection", meta = (EditCondition = "bUseHitscan", ClampMin = "0.0", ClampMax = "1.0"))
	float ReflectionEnergyLoss = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Reflection", meta = (EditCondition = "bUseHitscan"))
	TArray<TObjectPtr<UPhysicalMaterial>> MetalMaterials;

	// ==================== Wave Visualization ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Wave", meta = (EditCondition = "bUseHitscan"))
	bool bUseWaveVisualization = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Wave", meta = (EditCondition = "bUseHitscan && bUseWaveVisualization"))
	float Wavelength = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Wave", meta = (EditCondition = "bUseHitscan && bUseWaveVisualization"))
	float Amplitude = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Wave", meta = (EditCondition = "bUseHitscan && bUseWaveVisualization"))
	float BeamFadeTime = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Wave", meta = (EditCondition = "bUseHitscan && bUseWaveVisualization"))
	float WavePacketLength = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Wave", meta = (EditCondition = "bUseHitscan && bUseWaveVisualization"))
	float WavePacketDelay = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Wave", meta = (EditCondition = "bUseHitscan && bUseWaveVisualization"))
	float WavePacketSpeed = 50000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Wave", meta = (EditCondition = "bUseHitscan && bUseWaveVisualization"))
	int32 WaveFrontCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Wave", meta = (EditCondition = "bUseHitscan && bUseWaveVisualization"))
	float WaveFrontExpansionSpeed = 300.0f;

	// ==================== VFX ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UNiagaraSystem> MuzzleFlashFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX", meta = (EditCondition = "bUseHitscan"))
	TObjectPtr<UNiagaraSystem> BeamFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX", meta = (EditCondition = "bUseHitscan && bUseWaveVisualization"))
	TObjectPtr<UNiagaraSystem> WaveFrontFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX", meta = (EditCondition = "bUseHitscan"))
	TObjectPtr<UNiagaraSystem> ImpactFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX", meta = (EditCondition = "bUseHitscan"))
	TObjectPtr<UNiagaraSystem> ReflectionFX;

	// ==================== VFX|Muzzle Flash ====================

	/** ÃƒÂÃ…â€œÃƒÂÃ‚Â°Ãƒâ€˜Ã‚ÂÃƒâ€˜Ã‹â€ Ãƒâ€˜Ã¢â‚¬Å¡ÃƒÂÃ‚Â°ÃƒÂÃ‚Â± ÃƒÂÃ‚Â²Ãƒâ€˜Ã‚ÂÃƒÂÃ‚Â¿Ãƒâ€˜Ã¢â‚¬Â¹Ãƒâ€˜Ã‹â€ ÃƒÂÃ‚ÂºÃƒÂÃ‚Â¸ Ãƒâ€˜Ã†â€™ ÃƒÂÃ‚Â´Ãƒâ€˜Ã†â€™ÃƒÂÃ‚Â»ÃƒÂÃ‚Â° */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Muzzle Flash", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float MuzzleFlashScale = 1.0f;

	/** ÃƒÂÃ‚Â¦ÃƒÂÃ‚Â²ÃƒÂÃ‚ÂµÃƒâ€˜Ã¢â‚¬Å¡ ÃƒÂÃ‚Â²Ãƒâ€˜Ã‚ÂÃƒÂÃ‚Â¿Ãƒâ€˜Ã¢â‚¬Â¹Ãƒâ€˜Ã‹â€ ÃƒÂÃ‚ÂºÃƒÂÃ‚Â¸ Ãƒâ€˜Ã†â€™ ÃƒÂÃ‚Â´Ãƒâ€˜Ã†â€™ÃƒÂÃ‚Â»ÃƒÂÃ‚Â° */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Muzzle Flash")
	FLinearColor MuzzleFlashColor = FLinearColor(0.0f, 0.83f, 1.0f, 1.0f); // Cyan

	/** ÃƒÂÃ‹Å“ÃƒÂÃ‚Â½Ãƒâ€˜Ã¢â‚¬Å¡ÃƒÂÃ‚ÂµÃƒÂÃ‚Â½Ãƒâ€˜Ã‚ÂÃƒÂÃ‚Â¸ÃƒÂÃ‚Â²ÃƒÂÃ‚Â½ÃƒÂÃ‚Â¾Ãƒâ€˜Ã‚ÂÃƒâ€˜Ã¢â‚¬Å¡Ãƒâ€˜Ã…â€™ Ãƒâ€˜Ã‚ÂÃƒÂÃ‚Â²ÃƒÂÃ‚ÂµÃƒâ€˜Ã¢â‚¬Â¡ÃƒÂÃ‚ÂµÃƒÂÃ‚Â½ÃƒÂÃ‚Â¸Ãƒâ€˜Ã‚Â ÃƒÂÃ‚Â²Ãƒâ€˜Ã‚ÂÃƒÂÃ‚Â¿Ãƒâ€˜Ã¢â‚¬Â¹Ãƒâ€˜Ã‹â€ ÃƒÂÃ‚ÂºÃƒÂÃ‚Â¸ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Muzzle Flash", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float MuzzleFlashIntensity = 5.0f;

	/** ÃƒÂÃ¢â‚¬ÂÃƒÂÃ‚Â»ÃƒÂÃ‚Â¸Ãƒâ€˜Ã¢â‚¬Å¡ÃƒÂÃ‚ÂµÃƒÂÃ‚Â»Ãƒâ€˜Ã…â€™ÃƒÂÃ‚Â½ÃƒÂÃ‚Â¾Ãƒâ€˜Ã‚ÂÃƒâ€˜Ã¢â‚¬Å¡Ãƒâ€˜Ã…â€™ ÃƒÂÃ‚Â²Ãƒâ€˜Ã‚ÂÃƒÂÃ‚Â¿Ãƒâ€˜Ã¢â‚¬Â¹Ãƒâ€˜Ã‹â€ ÃƒÂÃ‚ÂºÃƒÂÃ‚Â¸ (Ãƒâ€˜Ã‚ÂÃƒÂÃ‚ÂµÃƒÂÃ‚Âº) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Muzzle Flash", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float MuzzleFlashDuration = 0.1f;

	// ==================== VFX|Colors ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Colors", meta = (EditCondition = "bUseHitscan"))
	FLinearColor BeamColor = FLinearColor(0.2f, 0.5f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX", meta = (EditCondition = "bUseHitscan && bUseWaveVisualization"))
	FLinearColor EFieldColor = FLinearColor(1.0f, 0.3f, 0.1f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX", meta = (EditCondition = "bUseHitscan && bUseWaveVisualization"))
	FLinearColor BFieldColor = FLinearColor(0.1f, 0.3f, 1.0f, 1.0f);

	// ==================== SFX ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX")
	TObjectPtr<USoundBase> FireSound;

	/** Sound attenuation settings for fire sound spatialization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX")
	TObjectPtr<USoundAttenuation> FireSoundAttenuation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float FireSoundPitchMin = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float FireSoundPitchMax = 1.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float FireSoundVolume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX", meta = (EditCondition = "bUseHitscan"))
	TObjectPtr<USoundBase> ReflectionSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|ADS")
	TObjectPtr<USoundBase> ADSInSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|ADS")
	TObjectPtr<USoundBase> ADSOutSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|ADS", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float ADSSoundPitchMin = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|ADS", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float ADSSoundPitchMax = 1.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|ADS", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ADSSoundVolume = 0.5f;

	// ==================== Animation ====================

	UPROPERTY(EditAnywhere, Category = "Animation")
	FName MuzzleSocketName = FName("Muzzle");

	UPROPERTY(EditAnywhere, Category = "Animation", meta = (ClampMin = 0, ClampMax = 100, Units = "cm"))
	float MuzzleOffset = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Animation")
	UAnimMontage* FiringMontage;

	UPROPERTY(EditAnywhere, Category = "Animation")
	TSubclassOf<UAnimInstance> FirstPersonAnimInstanceClass;

	UPROPERTY(EditAnywhere, Category = "Animation")
	TSubclassOf<UAnimInstance> ThirdPersonAnimInstanceClass;

	// ==================== ADS ====================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	bool bUseCustomADSOffset = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS", meta = (EditCondition = "bUseCustomADSOffset"))
	FVector CustomADSOffset = FVector(0.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS", meta = (ClampMin = "0", ClampMax = "120"))
	float CustomADSFOV = 0.0f;

	/** Socket name on weapon mesh for ADS camera position (e.g. "Sight" or "ADS") */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	FName ADSSocketName = FName("Sight");

	/** Second socket for ADS alignment - rear sight or stock. Both sockets will be placed on camera ray */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	FName ADSSocketNameRear = FName("SightRear");

	/** Third socket below rear socket - used to lock roll (keep weapon upright) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	FName ADSSocketNameBottom = FName("SightBottom");

	/** Relative location offset from socket for fine-tuning ADS camera position */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	FVector ADSCameraRelativeLocation = FVector::ZeroVector;

	/** Relative rotation offset from socket for fine-tuning ADS camera orientation */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	FRotator ADSCameraRelativeRotation = FRotator::ZeroRotator;

	/** Blend time when entering ADS (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float ADSBlendInTime = 0.15f;

	/** Blend time when exiting ADS (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float ADSBlendOutTime = 0.1f;

	// ==================== Recoil ====================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	bool bUseAdvancedRecoil = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (EditCondition = "bUseAdvancedRecoil"))
	FWeaponRecoilSettings RecoilSettings;

	// ==================== Ammo ====================

	UPROPERTY(EditAnywhere, Category = "Ammo", meta = (ClampMin = 1, ClampMax = 999))
	int32 MagazineSize = 30;

	int32 CurrentBullets = 0;

	// ==================== Refire ====================

	UPROPERTY(EditAnywhere, Category = "Refire", meta = (ClampMin = 0, ClampMax = 10, Units = "s"))
	float RefireRate = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Refire", meta = (ClampMin = 0, ClampMax = 10, Units = "deg"))
	float FiringRecoil = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Refire")
	bool bFullAuto = true;

	// ==================== Aim ====================

	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = 0, ClampMax = 10, Units = "deg"))
	float AimVariance = 1.0f;

	// ==================== State ====================

	bool bIsFiring = false;
	float TimeOfLastShot = 0.0f;
	FTimerHandle RefireTimer;
	APawn* PawnOwner;

	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> CachedMovementComponent;

	// ==================== Perception ====================

	UPROPERTY(EditAnywhere, Category = "Perception")
	float ShotNoiseRange = 5000.0f;

	UPROPERTY(EditAnywhere, Category = "Perception")
	float ShotLoudness = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Perception")
	FName ShotNoiseTag = FName("Shot");

public:

	/** Called when heat level changes */
	UPROPERTY(BlueprintAssignable, Category = "Heat System")
	FOnHeatChanged OnHeatChanged;

	/** Called when weapon fires a shot (for NPC burst counting) */
	UPROPERTY(BlueprintAssignable, Category = "Firing")
	FOnWeaponShotFired OnShotFired;

public:

	AShooterWeapon();

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	UFUNCTION()
	void OnOwnerDestroyed(AActor* DestroyedActor);

public:

	void ActivateWeapon();
	void DeactivateWeapon();
	void StartFiring();
	void StopFiring();

protected:

	virtual void Fire();
	void FireCooldownExpired();
	virtual void FireProjectile(const FVector& TargetLocation, float ChargeMultiplier = 1.0f);
	FTransform CalculateProjectileSpawnTransform(const FVector& TargetLocation) const;

	virtual void FireHitscan(const FVector& TargetLocation);
	void PerformHitscan(const FVector& Start, const FVector& Direction, float RemainingEnergy, int32 ReflectionCount);
	bool IsMetal(const FHitResult& Hit) const;
	FVector CalculateReflection(const FVector& Direction, const FVector& Normal) const;
	void ApplyHitscanDamage(const FHitResult& Hit, float EnergyMultiplier, float Distance, float WaveRadius);

	// ==================== Charge-Based Firing ====================

	/** Try to consume charge from owner. Returns false if cannot fire, sets OutChargeMultiplier for weak shots */
	bool TryConsumeCharge(float& OutChargeMultiplier);
	float CalculateWaveRadius(float Distance) const;
	float CalculateDamageMultiplier(float Distance, float WaveRadius) const;

	// ==================== Heat System ====================

	void UpdateHeat(float DeltaTime);
	void AddHeat(float Amount);
	float GetOwnerSpeed() const;
	float CalculateHeatDamageMultiplier() const;

	// ==================== Z-Factor ====================

	float CalculateZFactorMultiplier(float ShooterZ, float TargetZ) const;

	// ==================== VFX ====================

	UFUNCTION(BlueprintCallable, Category = "VFX")
	void SpawnMuzzleFlashEffect();

	UFUNCTION(BlueprintCallable, Category = "VFX")
	void SpawnBeamEffect(const FVector& Start, const FVector& End, float EnergyMultiplier = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "VFX")
	void SpawnWaveFronts(const FVector& Start, const FVector& End);

	UFUNCTION(BlueprintCallable, Category = "VFX")
	void SpawnImpactEffect(const FVector& Location, const FVector& Normal);

	UFUNCTION(BlueprintCallable, Category = "VFX")
	void SpawnReflectionEffect(const FVector& Location, const FVector& IncomingDirection, const FVector& ReflectedDirection);

	UFUNCTION(BlueprintCallable, Category = "SFX")
	void PlayFireSound();

public:

	UFUNCTION(BlueprintCallable, Category = "SFX")
	void PlayADSInSound();

	UFUNCTION(BlueprintCallable, Category = "SFX")
	void PlayADSOutSound();

	// ==================== Getters ====================

	UFUNCTION(BlueprintPure, Category = "Weapon")
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	USkeletalMeshComponent* GetThirdPersonMesh() const { return ThirdPersonMesh; }

	const TSubclassOf<UAnimInstance>& GetFirstPersonAnimInstanceClass() const;
	const TSubclassOf<UAnimInstance>& GetThirdPersonAnimInstanceClass() const;

	int32 GetMagazineSize() const { return MagazineSize; }
	int32 GetBulletCount() const { return CurrentBullets; }

	/** Set bullet count (used for checkpoint restore) */
	void SetBulletCount(int32 NewCount) { CurrentBullets = FMath::Clamp(NewCount, 0, MagazineSize); }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsHitscan() const { return bUseHitscan; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Hitscan")
	float GetOptimalDamageRange() const;

	UFUNCTION(BlueprintPure, Category = "Weapon|Hitscan")
	float GetWaveRadiusAtDistance(float Distance) const { return CalculateWaveRadius(Distance); }

	UFUNCTION(BlueprintPure, Category = "Weapon|Hitscan")
	float GetDamageMultiplierAtDistance(float Distance) const { return CalculateDamageMultiplier(Distance, CalculateWaveRadius(Distance)); }

	// ==================== Heat System Getters ====================

	/** ÃƒÂÃ¢â‚¬â„¢ÃƒÂÃ‚Â¾ÃƒÂÃ‚Â·ÃƒÂÃ‚Â²Ãƒâ€˜Ã¢â€šÂ¬ÃƒÂÃ‚Â°Ãƒâ€˜Ã¢â‚¬Â°ÃƒÂÃ‚Â°ÃƒÂÃ‚ÂµÃƒâ€˜Ã¢â‚¬Å¡ Ãƒâ€˜Ã¢â‚¬Å¡ÃƒÂÃ‚ÂµÃƒÂÃ‚ÂºÃƒâ€˜Ã†â€™Ãƒâ€˜Ã¢â‚¬Â°ÃƒÂÃ‚Â¸ÃƒÂÃ‚Â¹ Ãƒâ€˜Ã†â€™Ãƒâ€˜Ã¢â€šÂ¬ÃƒÂÃ‚Â¾ÃƒÂÃ‚Â²ÃƒÂÃ‚ÂµÃƒÂÃ‚Â½Ãƒâ€˜Ã…â€™ ÃƒÂÃ‚Â½ÃƒÂÃ‚Â°ÃƒÂÃ‚Â³Ãƒâ€˜Ã¢â€šÂ¬ÃƒÂÃ‚ÂµÃƒÂÃ‚Â²ÃƒÂÃ‚Â° (0-1) */
	UFUNCTION(BlueprintPure, Category = "Weapon|Heat")
	float GetCurrentHeat() const { return CurrentHeat; }

	/** ÃƒÂÃ¢â‚¬â„¢ÃƒÂÃ‚Â¾ÃƒÂÃ‚Â·ÃƒÂÃ‚Â²Ãƒâ€˜Ã¢â€šÂ¬ÃƒÂÃ‚Â°Ãƒâ€˜Ã¢â‚¬Â°ÃƒÂÃ‚Â°ÃƒÂÃ‚ÂµÃƒâ€˜Ã¢â‚¬Å¡ Ãƒâ€˜Ã¢â‚¬Å¡ÃƒÂÃ‚ÂµÃƒÂÃ‚ÂºÃƒâ€˜Ã†â€™Ãƒâ€˜Ã¢â‚¬Â°ÃƒÂÃ‚Â¸ÃƒÂÃ‚Â¹ ÃƒÂÃ‚Â¼ÃƒÂÃ‚Â½ÃƒÂÃ‚Â¾ÃƒÂÃ‚Â¶ÃƒÂÃ‚Â¸Ãƒâ€˜Ã¢â‚¬Å¡ÃƒÂÃ‚ÂµÃƒÂÃ‚Â»Ãƒâ€˜Ã…â€™ Ãƒâ€˜Ã†â€™Ãƒâ€˜Ã¢â€šÂ¬ÃƒÂÃ‚Â¾ÃƒÂÃ‚Â½ÃƒÂÃ‚Â° ÃƒÂÃ‚Â¾Ãƒâ€˜Ã¢â‚¬Å¡ ÃƒÂÃ‚Â½ÃƒÂÃ‚Â°ÃƒÂÃ‚Â³Ãƒâ€˜Ã¢â€šÂ¬ÃƒÂÃ‚ÂµÃƒÂÃ‚Â²ÃƒÂÃ‚Â° */
	UFUNCTION(BlueprintPure, Category = "Weapon|Heat")
	float GetHeatDamageMultiplier() const { return CalculateHeatDamageMultiplier(); }

	/** ÃƒÂÃ¢â‚¬â„¢ÃƒÂÃ‚Â¾ÃƒÂÃ‚Â·ÃƒÂÃ‚Â²Ãƒâ€˜Ã¢â€šÂ¬ÃƒÂÃ‚Â°Ãƒâ€˜Ã¢â‚¬Â°ÃƒÂÃ‚Â°ÃƒÂÃ‚ÂµÃƒâ€˜Ã¢â‚¬Å¡ true ÃƒÂÃ‚ÂµÃƒâ€˜Ã‚ÂÃƒÂÃ‚Â»ÃƒÂÃ‚Â¸ Ãƒâ€˜Ã‚ÂÃƒÂÃ‚Â¸Ãƒâ€˜Ã‚ÂÃƒâ€˜Ã¢â‚¬Å¡ÃƒÂÃ‚ÂµÃƒÂÃ‚Â¼ÃƒÂÃ‚Â° ÃƒÂÃ‚Â½ÃƒÂÃ‚Â°ÃƒÂÃ‚Â³Ãƒâ€˜Ã¢â€šÂ¬ÃƒÂÃ‚ÂµÃƒÂÃ‚Â²ÃƒÂÃ‚Â° ÃƒÂÃ‚Â²ÃƒÂÃ‚ÂºÃƒÂÃ‚Â»Ãƒâ€˜Ã…Â½Ãƒâ€˜Ã¢â‚¬Â¡ÃƒÂÃ‚ÂµÃƒÂÃ‚Â½ÃƒÂÃ‚Â° */
	UFUNCTION(BlueprintPure, Category = "Weapon|Heat")
	bool IsHeatSystemEnabled() const { return bUseHeatSystem; }

	// ==================== Z-Factor Getters ====================

	/** ÃƒÂÃ¢â‚¬â„¢ÃƒÂÃ‚Â¾ÃƒÂÃ‚Â·ÃƒÂÃ‚Â²Ãƒâ€˜Ã¢â€šÂ¬ÃƒÂÃ‚Â°Ãƒâ€˜Ã¢â‚¬Â°ÃƒÂÃ‚Â°ÃƒÂÃ‚ÂµÃƒâ€˜Ã¢â‚¬Å¡ true ÃƒÂÃ‚ÂµÃƒâ€˜Ã‚ÂÃƒÂÃ‚Â»ÃƒÂÃ‚Â¸ Z-Ãƒâ€˜Ã¢â‚¬Å¾ÃƒÂÃ‚Â°ÃƒÂÃ‚ÂºÃƒâ€˜Ã¢â‚¬Å¡ÃƒÂÃ‚Â¾Ãƒâ€˜Ã¢â€šÂ¬ ÃƒÂÃ‚Â²ÃƒÂÃ‚ÂºÃƒÂÃ‚Â»Ãƒâ€˜Ã…Â½Ãƒâ€˜Ã¢â‚¬Â¡ÃƒÂÃ‚ÂµÃƒÂÃ‚Â½ */
	UFUNCTION(BlueprintPure, Category = "Weapon|ZFactor")
	bool IsZFactorEnabled() const { return bUseZFactor; }

	// ==================== ADS Getters ====================

	UFUNCTION(BlueprintPure, Category = "Weapon|ADS")
	bool HasCustomADSOffset() const { return bUseCustomADSOffset; }

	UFUNCTION(BlueprintPure, Category = "Weapon|ADS")
	FVector GetADSOffset() const { return CustomADSOffset; }

	UFUNCTION(BlueprintPure, Category = "Weapon|ADS")
	float GetCustomADSFOV() const { return CustomADSFOV; }

	/** Returns the ADS camera component */
	UFUNCTION(BlueprintPure, Category = "Weapon|ADS")
	UCameraComponent* GetADSCamera() const { return ADSCameraComponent; }

	/** Returns ADS blend in time */
	UFUNCTION(BlueprintPure, Category = "Weapon|ADS")
	float GetADSBlendInTime() const { return ADSBlendInTime; }

	/** Returns ADS blend out time */
	UFUNCTION(BlueprintPure, Category = "Weapon|ADS")
	float GetADSBlendOutTime() const { return ADSBlendOutTime; }

	/** Updates ADS camera rotation to match owner's control rotation */
	void UpdateADSCameraRotation();

	/** Override to return ADS camera view for SetViewTarget */
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

	// ==================== ADS Weapon Attachment ====================

	/** Attach weapon mesh to camera for ADS (detaches from hands) */
	void AttachToCamera(UCameraComponent* Camera);

	/** Detach weapon mesh from camera and reattach to hands */
	void DetachFromCamera(USkeletalMeshComponent* HandsMesh, FName SocketName);

	/** Update weapon position during ADS transition */
	void UpdateADSTransition(float ADSAlpha, float DeltaTime);

	/** Returns true if weapon is currently attached to camera */
	bool IsAttachedToCamera() const { return bIsAttachedToCamera; }

protected:
	/** True when weapon mesh is attached to camera instead of hands */
	bool bIsAttachedToCamera = false;

	/** Saved transform relative to hands for returning from ADS */
	FTransform HipFireRelativeTransform;

	/** Current interpolated relative transform */
	FTransform CurrentADSTransform;

public:
	// ==================== Recoil Getters ====================

	UFUNCTION(BlueprintPure, Category = "Weapon|Recoil")
	bool UsesAdvancedRecoil() const { return bUseAdvancedRecoil; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Recoil")
	const FWeaponRecoilSettings& GetRecoilSettings() const { return RecoilSettings; }
};