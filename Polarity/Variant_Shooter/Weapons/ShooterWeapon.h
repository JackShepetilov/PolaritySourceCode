// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShooterWeaponHolder.h"
#include "Animation/AnimInstance.h"
#include "WeaponRecoilComponent.h"
#include "TutorialTypes.h"
#include "CrosshairConfig.h"
#include "WeaponSpreadConfig.h"
#include "MovementSettings.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Engine/NetSerialization.h"
#include "ShooterWeapon.generated.h"

class IShooterWeaponHolder;
class AShooterProjectile;
class USkeletalMeshComponent;
class UCameraComponent;
class UAnimMontage;
class UAnimInstance;
class UAnimationAsset;
class UNiagaraSystem;
class UNiagaraComponent;
class UPhysicalMaterial;
class UDamageType;
class UCharacterMovementComponent;
class USoundAttenuation;
class UEMF_FieldComponent;
class UInputAction;

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

	// The bolt subsystem drives deferred (dodgeable) hitscan damage and calls ApplyHitscanDamage.
	friend class UEnemyBeamBoltSubsystem;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* ThirdPersonMesh;

	/** Camera component placed at ADS sight socket — used as CalcCamera source during ADS */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* ADSCameraComponent;

protected:

	IShooterWeaponHolder* WeaponOwner;

	// ==================== Input ====================

	/** Input action that switches/equips this weapon (the per-weapon "hotkey"). Multiple weapon
	 *  classes may share one action (e.g. all ranged weapons → the same key) — only one is ever
	 *  owned at a time, so the selection is unambiguous. The character binds the union of these via
	 *  its WeaponSwitchActions list. Leave null for weapons reachable only via the cycle key. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SwitchAction;

	// ==================== Firing Mode ====================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Firing")
	bool bUseHitscan = false;

	// ==================== Dodgeable Bolt ====================
	// Enemy hitscan ALWAYS fires as a "bolt": a damage region that travels along the aim line at
	// HitscanBoltSpeed and only hurts the player if they're still on the line when it passes. The
	// Low-Health Defense upgrade slows it via the player's EnemyBoltSlowMultiplier (so it becomes
	// dodgeable at low HP). These values MUST mirror the enemy beam Niagara asset's Custom HLSL so
	// the visible tracer matches the damage region — expose Speed / SpeedVariance / beamLength as
	// User parameters on that asset (C++ pushes the effective values per shot).
	//
	// A PLAYER weapon can fire the same way, via bHitscanTravelsAsBolt below. That is what a shotgun
	// pellet is: a hit that is decided when the trigger is pulled and lands when it arrives. Speed
	// and length then describe the pellet itself, and the tracer is timed from them rather than
	// tuned separately, so the streak and the damage are the same object by construction.

	/** Fire this weapon's hits as travelling bolts instead of landing them instantly. Off leaves
	 *  the weapon an ordinary hitscan; the enemy path is unaffected either way, it always bolts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Firing|Bolt", meta = (EditCondition = "bUseHitscan"))
	bool bHitscanTravelsAsBolt = false;

	/** Default bolt travel speed (cm/s). Fast by default (≈ instant feel); the Low-Health Defense
	 *  upgrade multiplies it down (EnemyBoltSlowMultiplier) so bolts become dodgeable at low HP. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Firing|Bolt", meta = (ClampMin = "100.0"))
	float HitscanBoltSpeed = 30000.0f;

	/** Per-shot speed variance (cm/s): RandSpeed = HitscanBoltSpeed + variance * sin(RandomSeed). Must match the HLSL. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Firing|Bolt", meta = (ClampMin = "0.0"))
	float HitscanBoltSpeedVariance = 15000.0f;

	/** Length of the moving damage window along the beam (cm). Must match the HLSL beamLength. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Firing|Bolt", meta = (ClampMin = "1.0"))
	float HitscanBoltLength = 500.0f;

	/** Perpendicular tolerance (cm) from the beam line within which the player counts as hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Firing|Bolt", meta = (ClampMin = "1.0"))
	float HitscanBoltRadius = 80.0f;

	// ==================== Charge-Based Firing ====================

	/** If true, weapon consumes charge from owner to fire projectiles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Firing|Charge")
	bool bUseChargeFiring = false;

	/** Charge cost per shot (taken from owner's EMF charge) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Firing|Charge", meta = (EditCondition = "bUseChargeFiring", ClampMin = "0.0"))
	float ChargePerShot = 3.0f;

	/** Minimum charge module allowed (can still fire weak shots below this) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Firing|Charge", meta = (EditCondition = "bUseChargeFiring", ClampMin = "0.0"))
	float MinimumBaseCharge = 0.0f;

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

	/** How far above the headshot number one reported hit is still allowed to go.
	 *
	 *  A client computes its own damage and tells the server the result, so the server needs a number
	 *  to compare against. Charge weapons scale their shot by however much charge was spent, and
	 *  upgrades scale it further, so this is deliberately loose: it exists to catch a nonsense value,
	 *  not to second-guess the design. Anything above the ceiling is clamped, never dropped, so a
	 *  legitimate edge case costs a little damage instead of a whole hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Validation", meta = (ClampMin = "1.0"))
	float MaxReportedDamageMultiplier = 4.0f;


	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitscan", meta = (EditCondition = "bUseHitscan", ClampMin = "0"))
	float HitscanPhysicsForce = 100.0f;

	/** Draw debug visualization of hitscan shots.
	 *  Classic path (WaveDivergence == 0): camera trace ray (cyan), thin sweep corridor
	 *  (green = pawn damaged / red = nothing hit), pawn candidates (orange) and the chosen
	 *  target (green), wall hit (red), plus the visual tracer line from the muzzle (white) —
	 *  the gap between white and cyan is the muzzle parallax.
	 *  Cone path (WaveDivergence > 0): cone rings/axis, candidates green = damaged,
	 *  red = outside cone, orange = blocked by wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan", meta = (EditCondition = "bUseHitscan"))
	bool bDrawHitscanDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitscan", meta = (EditCondition = "bUseHitscan"))
	bool bHitscanDamageOwner = false;

	/** Damage multipliers based on target actor tags. Multiple matching tags multiply together. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan", meta = (EditCondition = "bUseHitscan"))
	TMap<FName, float> TagDamageMultipliers;

	/** This weapon only hurts a target whose shield is already down.
	 *
	 *  "Shield down" is the state the rest of the game already means by it: the target's charge has
	 *  reached its own ceiling (UEMFVelocityModifier::IsAtMaxCharge), which is the same instant the
	 *  enemy becomes grabbable. So a weapon with this on is a finisher: it charges the target like
	 *  any other, and does nothing to its health until somebody has filled that meter.
	 *
	 *  Everything except the damage still happens on a hit -- ionization, knockback, the hit marker
	 *  for an ionizing hit -- so the shot reads as landing rather than as passing through. A target
	 *  with no charge at all (no EMF component) has no shield to break and takes damage normally.
	 *
	 *  NOT under an EditCondition and NOT in the Hitscan category, both of which it used to be: the
	 *  melee weapon honours this too now, and a hitscan-gated checkbox on a sword is greyed out
	 *  forever with no way to tell that it would have worked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	bool bRequiresBrokenShieldToDamage = false;

	// ==================== Hitscan Ionization ====================

	/** If true, hitscan hits apply a fixed positive charge to the target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Ionization", meta = (EditCondition = "bUseHitscan"))
	bool bUseHitscanIonization = false;

	/** Fixed positive charge added to target per hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Ionization", meta = (EditCondition = "bUseHitscan && bUseHitscanIonization"))
	float IonizationChargePerHit = 2.0f;

	/** Maximum positive charge that can be applied via ionization (also used by laser) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitscan|Ionization", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float MaxIonizationCharge = 20.0f;

	/** True when the target is already at ITS OWN ceiling in the direction this weapon pushes.
	 *  Compares MAGNITUDES: a cap is a size, not a signed upper bound, and a weapon whose
	 *  IonizationChargePerHit is negative drives the charge the other way. */
	bool IsIonizationCapReached(float CurrentCharge, float Cap) const;

	/** The same test, for a caller whose per-hit amount is not IonizationChargePerHit. The melee
	 *  weapon carries its own, so the two-argument form above would silently judge a sword's step by
	 *  the hitscan number -- which on a sword is whatever the default happens to be. */
	bool IsIonizationCapReached(float CurrentCharge, float Cap, float ChargePerHit) const;

	/** One ionization step, clamped to +/-Cap so the charge has a ceiling whichever direction it is
	 *  being driven. The cap belongs to the target, not to this weapon. */
	float ApplyIonizationStep(float CurrentCharge, float Cap) const;

	/** The same step, with the per-hit amount handed in. See the three-argument cap test above. */
	float ApplyIonizationStep(float CurrentCharge, float Cap, float ChargePerHit) const;

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

	/** Maximum fire rate multiplier at maximum heat (2.0 = 2x slower fire rate at max heat) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat System", meta = (EditCondition = "bUseHeatSystem", ClampMin = "1.0", ClampMax = "5.0"))
	float MaxHeatFireRateMultiplier = 2.0f;

	/** Current heat level (0-1), read-only in BP */
	UPROPERTY(BlueprintReadOnly, Category = "Heat System")
	float CurrentHeat = 0.0f;

	// ==================== Heat VFX ====================

	/** Niagara system for heat effect on weapon (e.g., glow, smoke, sparks) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat System|VFX", meta = (EditCondition = "bUseHeatSystem"))
	TObjectPtr<UNiagaraSystem> HeatVFX;

	/** Socket name on weapon mesh to attach heat VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat System|VFX", meta = (EditCondition = "bUseHeatSystem"))
	FName HeatVFXSocket = NAME_None;

	/** Niagara parameter name for heat coefficient (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat System|VFX", meta = (EditCondition = "bUseHeatSystem"))
	FName HeatParameterName = FName("Heat");

	/** Minimum heat level to spawn VFX (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat System|VFX", meta = (EditCondition = "bUseHeatSystem", ClampMin = "0.0", ClampMax = "1.0"))
	float HeatVFXThreshold = 0.3f;

	/** Active heat VFX component */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> HeatVFXComponent;

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

	// ==================== VFX|Charge-Based Muzzle Flash ====================

	/** If true, use charge-based muzzle flash VFX instead of default MuzzleFlashFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Charge Muzzle Flash")
	bool bUseChargeMuzzleFlash = false;

	/** Muzzle flash VFX for positive charge (used when owner has positive EMF charge) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Charge Muzzle Flash", meta = (EditCondition = "bUseChargeMuzzleFlash"))
	TObjectPtr<UNiagaraSystem> PositiveMuzzleFlashFX;

	/** Muzzle flash VFX for negative charge (used when owner has negative EMF charge) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Charge Muzzle Flash", meta = (EditCondition = "bUseChargeMuzzleFlash"))
	TObjectPtr<UNiagaraSystem> NegativeMuzzleFlashFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX", meta = (EditCondition = "bUseHitscan"))
	TObjectPtr<UNiagaraSystem> BeamFX;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX", meta = (EditCondition = "bUseHitscan && bUseWaveVisualization"))
	TObjectPtr<UNiagaraSystem> WaveFrontFX;

	/** Default impact VFX (used when surface has no PhysicalMaterial or is missing from ImpactFXBySurface) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Impact", meta = (EditCondition = "bUseHitscan"))
	TObjectPtr<UNiagaraSystem> ImpactFX;

	/** Per-surface impact VFX. Key is the SurfaceType configured in Project Settings -> Physics -> Physical Surfaces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Impact", meta = (EditCondition = "bUseHitscan"))
	TMap<TEnumAsByte<EPhysicalSurface>, TObjectPtr<UNiagaraSystem>> ImpactFXBySurface;

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

	/** Sound played when trying to fire with insufficient charge (dry fire click) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX", meta = (EditCondition = "bUseChargeFiring"))
	TObjectPtr<USoundBase> DryFireSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX", meta = (EditCondition = "bUseHitscan"))
	TObjectPtr<USoundBase> ReflectionSound;

	// ==================== SFX|Feedback ====================

	/** How this weapon sounds when it lands a hit: the world impacts everyone hears and the
	 *  confirmations only the shooter hears, in one asset shared by a whole class of weapon.
	 *
	 *  The per-weapon maps below still win where they are filled in, so a gun with its own impacts
	 *  keeps them; the set fills every surface the weapon says nothing about. Leave it empty and
	 *  behaviour is exactly what it was before sets existed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Feedback")
	TObjectPtr<UHitFeedbackSet> FeedbackSet;

	// ==================== SFX|Impact ====================

	/** Default impact sound (used when surface has no PhysicalMaterial or is missing from ImpactSoundBySurface) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Impact", meta = (EditCondition = "bUseHitscan"))
	TObjectPtr<USoundBase> DefaultImpactSound;

	/** Per-surface impact sound. Key is the SurfaceType configured in Project Settings -> Physics -> Physical Surfaces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Impact", meta = (EditCondition = "bUseHitscan"))
	TMap<TEnumAsByte<EPhysicalSurface>, TObjectPtr<USoundBase>> ImpactSoundBySurface;

	/** Optional attenuation for impact sounds (3D spatialization, falloff) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Impact", meta = (EditCondition = "bUseHitscan"))
	TObjectPtr<USoundAttenuation> ImpactSoundAttenuation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Impact", meta = (EditCondition = "bUseHitscan", ClampMin = "0.5", ClampMax = "2.0"))
	float ImpactSoundPitchMin = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Impact", meta = (EditCondition = "bUseHitscan", ClampMin = "0.5", ClampMax = "2.0"))
	float ImpactSoundPitchMax = 1.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Impact", meta = (EditCondition = "bUseHitscan", ClampMin = "0.0", ClampMax = "2.0"))
	float ImpactSoundVolume = 1.0f;

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

	/** How long after sprinting ends this weapon needs to come up before it can fire. A shot asked
	 *  for during that window is deferred to the end of it, not dropped, so holding the trigger
	 *  through the raise fires the instant it opens. Releasing the trigger cancels it.
	 *  Keep this equal to the sprint-out blend time in the anim graph, or the weapon fires out of
	 *  a pose that has not finished coming up. Zero disables the gate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0.0", Units = "s"))
	float SprintToFireTime = 0.2f;

	// ==================== Animation ====================

	UPROPERTY(EditAnywhere, Category = "Animation")
	FName MuzzleSocketName = FName("Muzzle");

	UPROPERTY(EditAnywhere, Category = "Animation", meta = (ClampMin = 0, ClampMax = 100, Units = "cm"))
	float MuzzleOffset = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Animation")
	UAnimMontage* FiringMontage;

	// ==================== Animation|Weapon mesh ====================
	//
	// The two above are what the HOLDER plays: they run on the character's skeleton and move the
	// arms. These two run on the weapon's OWN skeleton -- the bolt, the pump, the cylinder, the
	// shell leaving the port. Different skeleton, different asset, and nothing plays them unless
	// they are set here.
	//
	// Either kind of asset works, but they are not equivalent:
	//   - a MONTAGE is played through the weapon mesh's own anim blueprint when it has one, so the
	//     graph keeps running and the montage blends into it and carries its notifies;
	//   - anything else (a plain sequence) is played straight on the component, which is what a
	//     weapon with no anim blueprint needs -- and which REPLACES the graph with a single-node
	//     player on a weapon that does have one. Give such a weapon a montage, not a sequence.

	/** Played on the weapon's own meshes each time it fires. */
	UPROPERTY(EditAnywhere, Category = "Animation|Weapon Mesh")
	TObjectPtr<UAnimationAsset> WeaponMeshFireAnimation;

	/** Played on the weapon's own meshes when a reload starts. */
	UPROPERTY(EditAnywhere, Category = "Animation|Weapon Mesh")
	TObjectPtr<UAnimationAsset> WeaponMeshReloadAnimation;

	/** Runs one of the above on both weapon meshes: the first person one the shooter sees and the
	 *  third person one everybody else sees. Does nothing when the asset is not set. */
	void PlayWeaponMeshAnimation(UAnimationAsset* Animation);

	/** Parents the ADS anchor to the best aiming reference this weapon actually has, trying in
	 *  order: an eye-point socket on a sight attachment (SightAimSocketName), a sight socket on the
	 *  weapon mesh (ADSSocketName), the sight MOUNT socket (ScopeMountSocketName), and finally
	 *  nothing at all, in which case the Blueprint's own placement is kept untouched.
	 *
	 *  Called on equip. Idempotent: re-running it re-derives the same attachment. */
	void ResolveADSAnchorAttachment();

	/** Copies each weapon mesh's render visibility (first person primitive type, owner see / owner
	 *  no see) onto everything parented under it, so an attachment added in the Blueprint cannot
	 *  end up drawn in a different pass from the gun it is bolted to. */
	void PropagateRenderVisibilityToChildren();

	UPROPERTY(EditAnywhere, Category = "Animation")
	TSubclassOf<UAnimInstance> FirstPersonAnimInstanceClass;

	UPROPERTY(EditAnywhere, Category = "Animation")
	TSubclassOf<UAnimInstance> ThirdPersonAnimInstanceClass;

	// ==================== ADS ====================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	bool bUseCustomADSOffset = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS", meta = (EditCondition = "bUseCustomADSOffset"))
	FVector CustomADSOffset = FVector(0.0f, 0.0f, 0.0f);

	/** THE zoom knob. How many times closer the sight picture is than the hip view: 1 = no zoom at
	 *  all, 2 = things look twice as big, 4 = a sniper scope.
	 *
	 *  A multiplier and not an angle, on purpose. An angle would have to be an angle relative to
	 *  SOMETHING, and the only "something" available is the player's own FOV setting, which the
	 *  player is free to move between 60 and 120. Storing 40 degrees here used to mean 2.75x zoom
	 *  for a player on 90 and NO zoom at all for a player on 60 — same weapon, same number, the
	 *  sights simply stopped working. A multiplier means the same thing at every setting.
	 *
	 *  Rough starting points: pistol/shotgun 1.2 to 1.5, rifle 1.5 to 2, marksman 3, sniper 4+. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ADS", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float ADSZoom = 1.5f;

	/** Socket name on weapon mesh for ADS camera position (e.g. "Sight" or "ADS").
	 *  Second choice in the chain, see ResolveADSAnchorAttachment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	FName ADSSocketName = FName("Sight");

	/** Eye-point socket carried by a SIGHT ATTACHMENT component parented under the weapon mesh.
	 *  First choice in the chain: a socket authored on the scope itself is the only one that is
	 *  actually a sight line rather than a mounting point, and it follows the scope when swapped.
	 *  Default matches the Low Poly Shooter Pack convention. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	FName SightAimSocketName = FName("SOCKET_Aim");

	/** Socket the weapon mesh offers for MOUNTING a sight. Last-resort anchor when no eye point
	 *  exists anywhere: it is on the rail, below and differently oriented to the real sight line,
	 *  so a weapon that falls through to this will usually need SightRotationOffset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	FName ScopeMountSocketName = FName("SOCKET_Scope");

	/** Align the sight AXIS with the camera while aiming, not just the sight POSITION.
	 *
	 *  Reads the ROTATION of ADSSocketName. Nothing read that rotation before this option existed,
	 *  so on a weapon whose socket was placed by eye for position alone it can be anything, and
	 *  turning this on will point the barrel somewhere new and wrong. Verify the socket (its +X
	 *  must run down the barrel), then leave this on. Off falls back to the old position-only ADS,
	 *  which cannot actually put the shot where the crosshair is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	bool bAlignSightRotation = true;

	/** Correction folded into the sight socket's rotation before ADS alignment uses it, applied in
	 *  the socket's own axes. Fixing the socket on the mesh is the better answer; this is for
	 *  weapons whose mesh cannot be re-authored. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS", meta = (EditCondition = "bAlignSightRotation"))
	FRotator SightRotationOffset = FRotator::ZeroRotator;

	/** Where the eye sits relative to the sight socket, in CAMERA axes: X forward, Y right, Z up.
	 *
	 *  The alignment on its own lands the sight socket exactly on the camera origin, and on a real
	 *  sight mesh that socket is the optic or the front post, not an eye point — so with a zero
	 *  offset the weapon ends up inside the player's head. X is therefore the eye relief and it is
	 *  the one that matters; Y and Z are there to nudge a socket that sits off the sight line.
	 *
	 *  This is the same knob the Low Poly Shooter Pack calls OffsetAiming ("we already perform
	 *  automatic calculations to aim perfectly through scopes, but this helps with adjusting"),
	 *  and their own data uses it heavily: 32 for every handgun, 16 to 18 for the rifles, 4 to 8.5
	 *  for the snipers. Ours are not their numbers — they move the ik_hand_gun bone and we move the
	 *  whole first person mesh — so measure per weapon rather than copying the pack.
	 *
	 *  Measuring it: the distance the sight socket has to travel FORWARD to reach the eye position
	 *  the weapon was hand-tuned for. On BP_ShooterWavePistol that is 61.21. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	FVector SightAimOffset = FVector::ZeroVector;

	/** Second socket for ADS alignment - rear sight or stock. Both sockets will be placed on camera ray */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	FName ADSSocketNameRear = FName("SightRear");

	/** Third socket below rear socket - used to lock roll (keep weapon upright) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	FName ADSSocketNameBottom = FName("SightBottom");

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

	// ==================== Reload ====================
	//
	// Off by default, and that default is the behaviour every weapon in the project had before this
	// existed: an empty magazine refills itself the instant it runs out, which is the same thing as
	// infinite ammunition with a cosmetic counter on the HUD. Switch bUseReload on and the magazine
	// becomes real -- it stays empty until it is filled, and filling it takes time the player can be
	// caught in. There is deliberately no reserve-ammo pool: a weapon either reloads out of thin air
	// or never runs out, and nothing in the game hands out boxes of ammunition yet.
	//
	// Yanked weapons (bHasLimitedAmmo) never reload whatever this says. They are thrown away when
	// they run dry, and that is their whole point.

	/** Does this weapon have a magazine that has to be reloaded, or does it never run out? */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo|Reload")
	bool bUseReload = false;

	/** How long the magazine takes to fill. Match it to the reload montage, or the weapon fires out
	 *  of an animation that has not finished. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo|Reload", meta = (EditCondition = "bUseReload", ClampMin = "0.05", Units = "s"))
	float ReloadTime = 2.0f;

	// Running out of ammunition and deciding to reload are two different events, and only the second
	// one is the player's. An empty magazine on its own does nothing here: the weapon is empty, it
	// says so by clicking, and it waits to be asked. Both shortcuts below exist because plenty of
	// shooters do take that decision for the player, but each one is off until somebody turns it on
	// deliberately, per weapon.

	/** Start reloading by itself the moment the magazine runs out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo|Reload", meta = (EditCondition = "bUseReload"))
	bool bAutoReloadWhenEmpty = false;

	/** Treat pulling the trigger on an empty magazine as asking to reload. Off means it just clicks:
	 *  a dry fire is an answer too, and it leaves the player in charge of when the gun goes down. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo|Reload", meta = (EditCondition = "bUseReload"))
	bool bReloadOnEmptyTriggerPull = false;

	/** Played on the holder for the duration of the reload, first and third person both. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo|Reload", meta = (EditCondition = "bUseReload"))
	TObjectPtr<UAnimMontage> ReloadMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo|Reload", meta = (EditCondition = "bUseReload"))
	TObjectPtr<USoundBase> ReloadSound;

	// ==================== Switch Animation (holster / draw) ====================
	// Four assets, because the two halves of a swap belong to two different weapons: the one going
	// away plays its Holster, the one coming out plays its Draw. First person is what the owner
	// sees, third person is what everybody else sees, and they are separate montages.
	//
	// Leave a montage empty and that half is simply skipped, which is the old instant swap. That is
	// the fallback on purpose: a weapon nobody has animated yet still works.

	/** FP arms montage for putting this weapon away. Place a "Weapon Switch - Swap Point" notify
	 *  where the old weapon should disappear and the new one appear; without one the swap happens
	 *  when the montage ends. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch Animation")
	TObjectPtr<UAnimMontage> HolsterMontage;

	/** Third-person montage for putting this weapon away, played on every machine. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch Animation")
	TObjectPtr<UAnimMontage> HolsterMontageTP;

	/** FP arms montage for bringing this weapon out. Firing, reloading and abilities come back when
	 *  it finishes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch Animation")
	TObjectPtr<UAnimMontage> DrawMontage;

	/** Third-person montage for bringing this weapon out, played on every machine. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch Animation")
	TObjectPtr<UAnimMontage> DrawMontageTP;

	/** How long putting this weapon away should take. The montage is stretched to fit by play rate,
	 *  so tuning the feel never means re-exporting the animation. 0 plays it at its authored speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch Animation", meta = (ClampMin = "0.0", ClampMax = "5.0", Units = "s"))
	float HolsterDuration = 0.0f;

	/** How long bringing this weapon out should take. Same play-rate scaling as HolsterDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch Animation", meta = (ClampMin = "0.0", ClampMax = "5.0", Units = "s"))
	float DrawDuration = 0.0f;

	/** True from the moment a reload starts until the magazine is full or the reload is cancelled. */
	bool bIsReloading = false;

	FTimerHandle ReloadTimer;

	/** The magazine is full and the weapon can shoot again. Resumes automatic fire if the trigger
	 *  was still held when the reload started. */
	void FinishReload();

	// ==================== Refire ====================

	UPROPERTY(EditAnywhere, Category = "Refire", meta = (ClampMin = 0, ClampMax = 10, Units = "s"))
	float RefireRate = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Refire", meta = (ClampMin = 0, ClampMax = 10, Units = "deg"))
	float FiringRecoil = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Refire")
	bool bFullAuto = true;

	/** Runtime multiplier on the refire interval, set by external owners (e.g. sentry turret spin-up).
	 *  1.0 = no change, <1.0 = faster fire, >1.0 = slower fire. Composes with the heat multiplier.
	 *  Not serialized as a default — owners drive it at runtime via SetExternalFireRateMultiplier. */
	float ExternalFireRateMultiplier = 1.0f;

	// ==================== Aim ====================

	/** BASE spread: the half-angle of the cone a shot leaves in when the owner is standing still,
	 *  hip-firing, and has not fired recently. Everything in SpreadConfig is expressed relative to
	 *  this, so a weapon that was already tuned keeps its character. */
	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = 0, ClampMax = 10, Units = "deg"))
	float AimVariance = 1.0f;

	// ==================== Spread ====================

	/** How the base spread above reacts to what the player is doing and to the trigger. See
	 *  WeaponSpreadConfig.h for the shape of the number. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim|Spread")
	FWeaponSpreadConfig SpreadConfig;

	/** The state multiplier as it is right now, chasing the one the owner's current state asks for.
	 *  Interpolated rather than snapped so stopping to shoot costs a beat. */
	float CurrentStateMultiplier = 1.0f;

	/** Degrees of spread added by shooting, on top of the state part. Bleeds off after
	 *  SpreadConfig.BloomRecoveryDelay. */
	float CurrentBloomDegrees = 0.0f;

	/** Game time of the shot the bloom is currently recovering from. */
	float TimeOfLastSpreadShot = -1000.0f;

	/** Moves CurrentStateMultiplier toward the owner's state and bleeds the bloom off. Called every
	 *  frame from Tick. */
	void UpdateSpread(float DeltaTime);

	/** The multiplier the owner's CURRENT state asks for, before interpolation: exactly one state
	 *  wins (air > slide > sprint > crouch > walk > still), then ADS scales it by the character's
	 *  ADS alpha. Returns StillMultiplier for an owner that is not a character. */
	float ResolveStateSpreadMultiplier() const;

	/** Adds one shot's worth of bloom and restarts the recovery delay. Called once per trigger pull
	 *  (a shotgun's pellets are one pull, not N). */
	void AddShotSpread();

	// ==================== Crosshair ====================

	/** Per-weapon HUD crosshair appearance (texture / tint / size). Read by the HUD crosshair widget
	 *  when this weapon is equipped. Purely visual. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosshair")
	FCrosshairConfig CrosshairConfig;

	/** Single entry point for every hit this weapon lands.
	 *  On the authority it applies damage directly, exactly as before. On a client owned by a
	 *  player it hands the hit to the owning character, which reports it to the server: a client
	 *  writing HP locally would only kill its own copy of the target.
	 *  Returns the damage that was actually applied on the authority, or the requested damage on
	 *  a client, where the true number only comes back later as replicated health. */
	float ApplyDamageToTarget(AActor* HitActor, float FinalDamage, const struct FDamageEvent& DamageEvent);
	/** True when Target's shield reads empty, i.e. its charge is at its own ceiling. Answered with
	 *  the same components and the same ceilings the ionization uses, so "the weapon can hurt it now"
	 *  and "the meter is full" can never disagree. Targets that carry no charge at all count as
	 *  having no shield. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Hitscan")
	bool IsTargetShieldDown(AActor* Target) const;


	// ==================== State ====================

	bool bIsFiring = false;
	float TimeOfLastShot = 0.0f;
	FTimerHandle RefireTimer;
	APawn* PawnOwner;

	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> CachedMovementComponent;

	// ==================== Tutorial ====================

	/**
	 * Tutorial ID for first-equip slide.
	 * If not None, a tutorial slide will be shown the first time this weapon is equipped.
	 * Uses TutorialSubsystem completion tracking - shows only once ever.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FName FirstEquipTutorialID;

	/** Slide data shown on first equip */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FTutorialSlideData FirstEquipSlideData;

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

	// ==================== First Person View Pose ====================

	/** Static rotation offset applied to the FP mesh while this weapon is equipped (neutral state only — replaced by crouch/wallrun tilts). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View Pose")
	FRotator FirstPersonMeshTilt = FRotator::ZeroRotator;

	/** Static location offset applied to the FP mesh while this weapon is equipped (neutral state only — replaced by crouch/wallrun offsets). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View Pose")
	FVector FirstPersonMeshOffset = FVector::ZeroVector;

	/** How the spine bends while this weapon is reloading, faded in for as long as the reload
	 *  ANIMATION is playing. Per weapon, because every reload animation is turned differently: the
	 *  point of it is to bring the magazine and the hands into frame, and where they are depends on
	 *  the animation. Handed to the anim graph together with the movement states, so a reload during
	 *  a wallrun is the sum of the two. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View Pose")
	FFirstPersonSpinePose ReloadSpinePose;


public:

	AShooterWeapon();

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	/** Override CalcCamera so that SetViewTarget(Weapon) produces a clean ADS camera view.
	 *  Uses the ADS sight socket position but ControlRotation (ignoring recoil visual kick). */
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

	UFUNCTION()
	void OnOwnerDestroyed(AActor* DestroyedActor);

public:

	void ActivateWeapon();
	void DeactivateWeapon();
	void StartFiring();
	void StopFiring();

	/** Fire exactly one shot now, ignoring the auto/refire cadence — for animation-notify-driven
	 *  firing. Routes through Fire() (aim, ammo, charge, OnShotFired), then clears any scheduled
	 *  refire so the cadence is owned by the animation. */
	void FireOnce();

	// ==================== Reload ====================

	/** Begin filling the magazine. Returns false and does nothing when there is nothing to do:
	 *  a weapon without a magazine, one already reloading, one already full, or a yanked weapon,
	 *  which is thrown away rather than reloaded. Safe to call from anywhere, so the reload key can
	 *  be pressed at any time without the caller checking first. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Reload")
	bool StartReload();

	/** True when StartReload would actually start one. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Reload")
	bool CanReload() const;

	UFUNCTION(BlueprintPure, Category = "Weapon|Reload")
	bool IsReloading() const { return bIsReloading; }

	/** True when this weapon has a magazine at all. False means it never runs out. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Reload")
	bool UsesReload() const { return bUseReload; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Reload")
	float GetReloadTime() const { return ReloadTime; }

	/** The montage this weapon plays while reloading, if it has one. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Reload")
	UAnimMontage* GetReloadMontage() const { return ReloadMontage; }

	// ==================== Switch Animation accessors ====================

	UFUNCTION(BlueprintPure, Category = "Weapon|Switch Animation")
	UAnimMontage* GetHolsterMontage() const { return HolsterMontage; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Switch Animation")
	UAnimMontage* GetHolsterMontageTP() const { return HolsterMontageTP; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Switch Animation")
	UAnimMontage* GetDrawMontage() const { return DrawMontage; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Switch Animation")
	UAnimMontage* GetDrawMontageTP() const { return DrawMontageTP; }

	/** Play rate that makes Montage last exactly Duration seconds. 1.0 when either is unset, so an
	 *  unfilled duration means "as authored" rather than "instant". */
	static float GetSwitchPlayRate(const UAnimMontage* Montage, float Duration);

	/** Play rate for this weapon's holster half. @see GetSwitchPlayRate */
	UFUNCTION(BlueprintPure, Category = "Weapon|Switch Animation")
	float GetHolsterPlayRate() const { return GetSwitchPlayRate(HolsterMontage, HolsterDuration); }

	/** Play rate for this weapon's draw half. @see GetSwitchPlayRate */
	UFUNCTION(BlueprintPure, Category = "Weapon|Switch Animation")
	float GetDrawPlayRate() const { return GetSwitchPlayRate(DrawMontage, DrawDuration); }

	/** Wall-clock length of the holster half, after the play rate. 0 when there is no montage. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Switch Animation")
	float GetHolsterLength() const;

	/** Wall-clock length of the draw half, after the play rate. 0 when there is no montage. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Switch Animation")
	float GetDrawLength() const;

	/** How far along the current reload is, 0 to 1. Zero when not reloading, for a HUD bar. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Reload")
	float GetReloadProgress() const;

	/** Drop a reload in progress and leave the magazine as it was. Called when the weapon is put
	 *  away; the round does not go in if the gun is no longer in the player's hands. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Reload")
	void CancelReload();

	/** Returns true if this weapon is a melee weapon (blocks MeleeAttackComponent while equipped) */
	virtual bool IsMeleeWeapon() const { return false; }

	/** Called when ADS/secondary action button pressed. Return true to block normal ADS. */
	virtual bool OnSecondaryAction();

	/** Called when ADS/secondary action button released. */
	virtual void OnSecondaryActionReleased();

protected:

	virtual void Fire();
	void FireCooldownExpired();
	virtual void FireProjectile(const FVector& TargetLocation, float ChargeMultiplier = 1.0f);

	FTransform CalculateProjectileSpawnTransform(const FVector& TargetLocation) const;

	/** Launch direction that actually lands a falling projectile on TargetLocation, or a zero vector
	 *  when the straight line is the right answer after all.
	 *
	 *  Hooks itself in rather than being switched on per weapon: it lives on the projectile spawn
	 *  path, so it only ever runs for projectile weapons by construction, and it asks the projectile
	 *  class itself whether it has gravity and how fast it flies. A weapon whose projectile does not
	 *  fall gets a zero back and keeps firing straight, with no configuration anywhere saying so.
	 *
	 *  AI only. The player aims with a crosshair, and bending their shot toward whatever the aim
	 *  trace happened to hit is aim assist, not ballistics - it would take the shot away from them
	 *  at exactly the moment they were trying to lead it themselves. */
	FVector SolveBallisticAim(const FVector& LaunchLocation, const FVector& TargetLocation) const;

	/** How far above a flat trajectory this weapon lobs. Zero fires as flat as gravity permits,
	 *  which is what a rocket wants; a grenade launcher wants the high solution so its shells clear
	 *  the cover between it and whoever it is shelling.
	 *
	 *  Only consulted when the projectile actually falls, so it costs nothing on a flat weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Ballistics")
	bool bLobProjectiles = false;

	/** Steepest launch a lob is allowed to come out at before it gives up and fires flat instead.
	 *
	 *  The high-arc solution degenerates towards vertical whenever the projectile carries far more
	 *  speed than the range needs, so without a ceiling a fast shell aimed at a near target is
	 *  launched at the sky and lands back on the shooter's own head. Fifty degrees still clears
	 *  ordinary cover and still reads as a lobbed shot; past that it reads as a mistake.
	 *
	 *  If a weapon genuinely wants mortar angles, the honest lever is a SLOWER projectile, not a
	 *  higher ceiling: at a speed matched to its range the high solution is naturally steep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Ballistics", meta = (EditCondition = "bLobProjectiles", ClampMin = "0.0", ClampMax = "89.0"))
	float MaxLobPitchDegrees = 50.0f;

	/** How far ahead of the muzzle the arc has to be clear before it is used. Short on purpose: this
	 *  asks whether the round can leave the barrel, not whether the whole trajectory is clean. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Ballistics", meta = (ClampMin = "0.0"))
	float MuzzleClearanceDistance = 200.0f;

	/** Log every ballistic solve: what it was asked to hit, what it decided to hit, and the angle it
	 *  came out at. Filter the Output Log on [BALLISTIC_DEBUG]. Off by default - this is one line per
	 *  shot per enemy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Ballistics")
	bool bLogBallistics = false;

	virtual void FireHitscan(const FVector& TargetLocation);

	/** Where a hitscan shot starts and which way it points, before anything is traced: muzzle or
	 *  camera viewpoint, aim point or view direction, plus AimVariance.
	 *
	 *  Split out of FireHitscan so a weapon that puts several pellets in the air on one trigger pull
	 *  can resolve the aim line once and then send its own directions through the same tracing. */
	void ResolveHitscanRay(const FVector& TargetLocation, FVector& OutStart, FVector& OutDirection) const;

	/** Where the first-person muzzle is DRAWN, in world space.
	 *
	 *  A first-person primitive is not rendered where it stands. FViewMatrices::Init scales its
	 *  view-space position by (FOVCorr, FOVCorr, 1) * FirstPersonScale, where
	 *  FOVCorr = tan(FOV/2) / tan(FirstPersonFOV/2) -- that is what lets the weapon keep its own
	 *  field of view while the world has another one. So the muzzle socket's world position is NOT
	 *  where the barrel appears on screen.
	 *
	 *  Anything attached to the mesh is drawn with the mesh and lines up for free, which is why the
	 *  muzzle flash has always been right. Anything spawned at a world position instead -- the
	 *  tracer -- starts wherever the socket physically is, which is somewhere else entirely. This
	 *  puts the socket through the same transform the renderer uses, so a world-space effect can be
	 *  placed where the barrel looks like it is. Returns the plain socket location for anything that
	 *  is not a local player's first-person view. */
	FVector GetFirstPersonMuzzleRenderLocation() const;

	/** What one trigger pull costs, whatever it put in the air: the firing montage, the recoil kick,
	 *  a round out of the magazine (refilled or discarded when it runs out) and the HUD that shows
	 *  it. Called ONCE per shot -- a shotgun's pellets are one shot, not three. */
	void ConsumeRoundAfterShot();

	void PerformHitscan(const FVector& Start, const FVector& Direction, float RemainingEnergy, int32 ReflectionCount);

	/** Classic thin-ray hitscan, used when WaveDivergence == 0 (PerformHitscan dispatches here).
	 *  At zero divergence the cone pipeline degenerates: the filter's dot branch needs
	 *  DotProduct >= cos(0) = 1.0 (never true in float), and its radius branch compares the
	 *  contact point on the capsule SURFACE (up to ~34u off axis) against InitialWaveRadius (~5u),
	 *  so legitimate hits are rejected. This path damages the NEAREST pawn on the ray instead.
	 *  Expects Start at the camera viewpoint for primary player shots (see FireHitscan) so the
	 *  ray matches the crosshair; the beam VFX is still drawn from the muzzle. Wall damage,
	 *  metal reflections, knockback and ionization behave like the cone path. */
	void PerformClassicHitscan(const FVector& Start, const FVector& Direction, float RemainingEnergy, int32 ReflectionCount);

	/** NPC simple hitscan: straight line trace without cone sweep.
	 *  Bypasses the cone-based system which has parallax issues for NPCs
	 *  (camera and muzzle are at different positions, causing the cone check to reject valid hits). */
	void PerformSimpleHitscan(const FVector& Start, const FVector& Direction, float EnergyMultiplier);

	bool IsMetal(const FHitResult& Hit) const;
	FVector CalculateReflection(const FVector& Direction, const FVector& Normal) const;
	/** ExtraDamageMultiplier carries what a bolt cannot re-derive when it lands: heat, height
	 *  advantage, target tags and the shooter's upgrades, all folded into one number at the moment
	 *  the trigger was pulled. One for an ordinary instant hit, which computes them itself. */
	void ApplyHitscanDamage(const FHitResult& Hit, float EnergyMultiplier, float Distance, float WaveRadius,
		float ExtraDamageMultiplier = 1.0f);

	/** Calculate damage multiplier based on target's tags */
	float GetTagDamageMultiplier(AActor* Target) const;

public:
	/** Apply ionization (fixed positive charge) to a hit target.
	 *  HitComponent is used by the NPC riot-shield rule: when an active shield is up,
	 *  only hits on the shield mesh transfer charge to the NPC body — direct body hits
	 *  bypass ionization entirely. Pass `FHitResult::GetComponent()` from the hitscan trace.
	 *
	 *  Public because a client's ionization has to be re-applied on the authority, which happens in
	 *  AShooterCharacter::Server_ReportIonization rather than here. */
	bool ApplyHitscanIonization(AActor* Target, UPrimitiveComponent* HitComponent = nullptr);

	/** Ionization with the per-hit amount handed in, and WITHOUT the bUseHitscanIonization gate.
	 *
	 *  This is the whole of what ionizing a target means -- the riot-shield rule, the report to the
	 *  authority, the upgrade notification, the shield-bypass redirect into health, and the walk
	 *  through the three kinds of chargeable thing. ApplyHitscanIonization is now just its own flag
	 *  and its own number in front of this, and the melee weapon puts its own flag and its own number
	 *  in front of the same body.
	 *
	 *  Shared rather than copied deliberately: the last time the shield-bypass redirect was added to
	 *  one ionization path and not the other, the mechanic worked for exactly one weapon. */
	bool ApplyIonizationToTarget(AActor* Target, UPrimitiveComponent* HitComponent, float ChargePerHit);

	/** True when this weapon is a finisher and the target's shield is still up, so its damage must be
	 *  withheld. The hit itself is not cancelled by this -- ionization, knockback and the hit marker
	 *  all still run, which is what makes charging a target feel like progress. */
	bool ShouldWithholdDamageForShield(AActor* Target) const;

protected:

	// ==================== Charge-Based Firing ====================

	/** Try to consume charge from owner. Returns false if cannot fire, sets OutChargeMultiplier for weak shots */
	bool TryConsumeCharge(float& OutChargeMultiplier);
	float CalculateWaveRadius(float Distance) const;
	float CalculateDamageMultiplier(float Distance, float WaveRadius) const;

	// ==================== Heat System ====================

	void UpdateHeat(float DeltaTime);
	void UpdateHeatVFX();
	void AddHeat(float Amount);
	float GetOwnerSpeed() const;
	float CalculateHeatDamageMultiplier() const;
	float CalculateHeatFireRateMultiplier() const;
	virtual float GetCurrentRefireRate() const;

	// ==================== Z-Factor ====================

	float CalculateZFactorMultiplier(float ShooterZ, float TargetZ) const;

	// ==================== VFX ====================

	UFUNCTION(BlueprintCallable, Category = "VFX")
	void SpawnMuzzleFlashEffect();

	/** Get owner's EMF charge value. Returns 0 if owner has no EMF component. */
	float GetOwnerCharge() const;

	/** Spawn the beam tracer. The optional Override* params feed the low-HP dodgeable-bolt
	 *  values (Speed / SpeedVariance / beamLength) and a fixed RandomSeed into the Niagara asset
	 *  so the visible tracer matches the C++ damage region. Pass < 0 to leave the asset defaults. */
	/** Tracer. Plays here and, like the muzzle flash, on every other machine that can see this
	 *  weapon: the beam used to be purely local, so a teammate's shots left no trail at all. */
	/** Returns the tracer drawn on THIS machine, so a caller that knows more about the shot than the
	 *  tracer does can put it out early -- a travelling shot hands it to its bolt, which stops it
	 *  where the pellet actually stopped. Null on a weapon with no BeamFX. */
	UFUNCTION(BlueprintCallable, Category = "VFX")
	UNiagaraComponent* SpawnBeamEffect(const FVector& Start, const FVector& End, float EnergyMultiplier = 1.0f,
		float OverrideBoltSpeed = -1.0f, float OverrideBoltSpeedVariance = -1.0f,
		float OverrideBoltLength = -1.0f, float OverrideRandomSeed = -1.0f);

	/** The actual spawn, with no networking. Shared by the local call and the multicast. */
	UNiagaraComponent* SpawnBeamEffectLocally(const FVector& Start, const FVector& End, float EnergyMultiplier,
		float OverrideBoltSpeed, float OverrideBoltSpeedVariance,
		float OverrideBoltLength, float OverrideRandomSeed);

	UFUNCTION(BlueprintCallable, Category = "VFX")
	void SpawnWaveFronts(const FVector& Start, const FVector& End);

	/** Resolve, play locally, then tell everyone else. Same split as the muzzle flash and the
	 *  tracer: the shooter must not wait a round trip to see their own bullet land. */
	UFUNCTION(BlueprintCallable, Category = "VFX")
	void SpawnImpactEffect(const FHitResult& Hit);

	/** The impact on this machine. Takes an already-resolved surface rather than a hit result
	 *  because only the shooter can decide it -- IsTargetShieldDown is read at the instant of the
	 *  hit, and an observer re-deriving it a round trip later would answer differently and show a
	 *  shield spark where the shooter saw blood. */
	void SpawnImpactEffectLocally(const FVector& Location, const FVector& Normal, EPhysicalSurface Surface);

	/** Which surface a hit should sound and look like: the physical material, unless the target is
	 *  an NPC that answers for its own shield-versus-body surface. */
	EPhysicalSurface ResolveImpactSurface(const FHitResult& Hit) const;

	UFUNCTION(BlueprintCallable, Category = "VFX")
	void SpawnReflectionEffect(const FVector& Location, const FVector& IncomingDirection, const FVector& ReflectedDirection);

	UFUNCTION(BlueprintCallable, Category = "SFX")
	void PlayFireSound();

	/** Muzzle flash + fire sound on this machine. The shared body of the local call and the
	 *  multicast, so the effects can never drift apart between owner and observers. */
	void PlayFireEffectsLocally();

	/** The weapon's own reload on this machine: the moving parts and the sound. Same split as the
	 *  firing effects above, for the same reason. */
	void PlayReloadEffectsLocally();

public:
	/** Muzzle flash and fire sound, played on every machine that can see this weapon.
	 *  Cosmetic only, so it is unreliable: a dropped shot effect is better than a stalled channel
	 *  during sustained fire. Multicast originates on the authority, which is why the firing client
	 *  plays its own effects locally first instead of waiting for the round trip.
	 *  Public because the owning character relays a client's shot through it. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayFireEffects();

	/** The magazine coming out, the pump, the shells going in, played on every machine that can see
	 *  this weapon. Everyone but the owner only ever has the third person mesh, so without this the
	 *  gun sat perfectly still through a teammate's or an enemy's whole reload.
	 *  Reliable, unlike the firing one: this happens once per magazine rather than once per shot,
	 *  and a dropped one leaves a visibly dead weapon for the entire reload. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayReloadEffects();

	/** Tracer for everyone else. Endpoints travel with it because only the shooter traced them. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayBeamEffect(const FVector& Start, const FVector& End, float EnergyMultiplier,
		float OverrideBoltSpeed, float OverrideBoltSpeedVariance,
		float OverrideBoltLength, float OverrideRandomSeed);

	/** The bullet landing, for everyone else. Where the shot ends is the loudest thing in a fight
	 *  after the shot itself, and until this existed a teammate's rounds hit the world in silence.
	 *
	 *  Carries the resolved surface rather than the hit, because the surface depends on whether the
	 *  target's shield was up at the moment of the hit and only the shooter was there for it.
	 *  Unreliable and quantized: this is one cosmetic event per bullet. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayImpactEffect(FVector_NetQuantize100 Location, FVector_NetQuantizeNormal Normal, uint8 SurfaceByte);

protected:

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

	/** Where shots physically leave this weapon, in world space.
	 *
	 *  The one place that answers it. Aiming, permission to fire and the projectile spawn were each
	 *  measuring from a different point - the NPC's camera, the middle of its capsule, and this
	 *  socket - and around a corner those three disagree: the camera and the capsule can be past the
	 *  edge while the barrel is still behind it. The shot was cleared by one point and born at
	 *  another, so it hit the wall the NPC had just stepped out of.
	 *
	 *  Returns the actor location when there is no mesh or no socket, so a caller never has to
	 *  handle a zero vector. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	FVector GetMuzzleWorldLocation() const;

	/** Can a shot fired right now actually get out and reach TargetLocation.
	 *
	 *  Not the same question as "is there a line of sight", and the difference is the whole bug it
	 *  exists for. A projectile is a SPHERE: it needs a corridor as wide as itself, while a zero
	 *  width ray slips through the gap between a barrel and the edge of a corner that the shell
	 *  cannot. An NPC leaning out of cover passes the ray test roughly half a second before the
	 *  round could survive the trip, and every shot fired in that window detonates on the corner it
	 *  is stepping around - reliably, every cycle, from the same spot, which is why it never looked
	 *  like scatter.
	 *
	 *  Swept with the projectile's own collision radius for projectile weapons, and a plain line for
	 *  hitscan, which really is infinitely thin. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool CanShotReach(const FVector& TargetLocation) const;

	// ==================== Grip alignment ====================
	//
	// How a weapon sits in a hand. Whoever is holding it attaches the mesh to a hand socket first,
	// then calls this, and the mesh is moved and turned so that its grip socket lands exactly on the
	// hand socket, matching it in orientation as well as position. That makes the grip socket the
	// one place where "how this weapon is held" is authored: turn the socket in the mesh editor and
	// the weapon turns in the hand. Player and NPCs both go through here, so they hold alike.

	/** The grip socket every weapon may carry. First person always uses this one. */
	static const FName OptionalGripSocketName;

	/** Suffix that marks a socket as the third person variant of another one. */
	static const FName ThirdPersonSocketSuffix;

	/** BaseSocket with "_TP" appended when the mesh carries it, BaseSocket otherwise.
	 *
	 *  This is how a weapon can be held one way on camera and another way on the body without the
	 *  two settings fighting each other. First person is tuned by hand per weapon against the
	 *  camera; third person has to look right to everybody else. Author OptionalGrip_TP to turn the
	 *  gun in the hand, GripPoint_002_TP to move where the off hand grabs it, and first person keeps
	 *  using OptionalGrip and GripPoint_002 as before. Add neither and nothing changes. */
	static FName PickThirdPersonSocket(const USkeletalMeshComponent* WeaponMesh, const FName BaseSocket);

	/** Lands GripSocket on the socket WeaponMesh is attached to. Does nothing if there is no such
	 *  socket, in which case the mesh keeps hanging by its own origin. Logs under [GRIP_DEBUG]. */
	static void AlignMeshToGripSocket(USkeletalMeshComponent* WeaponMesh, const FName GripSocket);

	/** Puts one projectile in the world at a transform somebody else already decided on. Public
	 *  because the authoritative one is spawned from the owning character's server RPC, after that
	 *  RPC has checked the request.
	 *
	 *  Cosmetic ones are the shooter's local stand-in and come from the pool; the authoritative one
	 *  is spawned outright, because a pooled actor is reused and a replicated actor must not be. */
	AShooterProjectile* SpawnProjectileAtTransform(const FTransform& ProjectileTransform,
		float ChargeMultiplier, bool bCosmeticOnly);

	// ==================== Server-side validation ====================

	/** The most one hit from this weapon may legitimately be worth. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Validation")
	float GetMaxReportedSingleHitDamage() const;

	/** How far this weapon reaches. Read by the server when it checks a reported hit. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Validation")
	float GetMaxHitscanRange() const { return MaxHitscanRange; }

	/** What a body shot at full energy would deal to Target right now, with the whole owner-side
	 *  multiplier stack applied: heat, height, tags, upgrades and the class passive.
	 *
	 *  For DISPLAY, not for damage. It cannot include the two factors that only a real shot knows —
	 *  the bone it lands on and the energy it has left — so it is deliberately the plain body-shot
	 *  number, which is the one worth comparing two of. Zero for a weapon with no hitscan damage.
	 *  @see UAbilityHandler::GetPredictedShotDamage. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Damage")
	float PredictDamageAgainst(AActor* Target) const;

	/** True when this weapon's own damage is being held back by the target's shield. Its own function
	 *  because two damage paths ask it — the direct one here and the server's re-check of a hit a
	 *  client reported — and they must not be able to answer it differently. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Damage")
	bool IsShieldGateBlocking(AActor* HitActor) const;

	/** Apply the owner's class passive's own damage for one hit, straight to health and past the
	 *  shield gate. Authority only; returns what actually landed, 0 when there is no such passive.
	 *  @see UAbilityHandler::GetBonusPierceDamage */
	float ApplyPassivePierceDamage(AActor* HitActor);

	/** Writes LeftHandIKTransform and LeftHandIKAlpha on an anim instance, looked up by name. Any
	 *  anim blueprint that declares those two picks it up and anything else is left untouched, which
	 *  is what lets players and NPCs share one call even when they run different graphs. */
	static void PushLeftHandIK(UAnimInstance* AnimInstance, const FTransform& Transform, float Alpha);

	// ==================== Yank Origin Tag ====================

	/** True when this weapon was acquired by yanking it from a HumanoidNPC.
	 *  Player can hold at most one yanked weapon at a time — taking a new one drops the old.
	 *  Set in ADroppedRangedWeapon::CompletePull after the weapon is added to the inventory. */
	UPROPERTY(BlueprintReadOnly, Category = "Yank")
	bool bWasYanked = false;

	/** ADroppedRangedWeapon class to spawn when this weapon is discarded by being replaced
	 *  via another yank. Set together with bWasYanked at pickup time. */
	UPROPERTY(BlueprintReadOnly, Category = "Yank")
	TSubclassOf<class ADroppedRangedWeapon> SourceYankDropClass;

	/** True when this weapon has finite ammo and must be discarded when CurrentBullets reaches 0
	 *  (no manual reload). Set in ADroppedRangedWeapon::CompletePull only when the source drop
	 *  was yank-spawned (SpawnedBulletCount > 0). Leaves the auto-refill behavior intact for
	 *  starter weapons and NPC death drops, which both keep this flag at false. */
	UPROPERTY(BlueprintReadOnly, Category = "Yank")
	bool bHasLimitedAmmo = false;

	const TSubclassOf<UAnimInstance>& GetFirstPersonAnimInstanceClass() const;
	const TSubclassOf<UAnimInstance>& GetThirdPersonAnimInstanceClass() const;

	int32 GetMagazineSize() const { return MagazineSize; }
	int32 GetBulletCount() const { return CurrentBullets; }

	/** Input action that switches/equips this weapon (per-weapon hotkey). May be null. */
	UInputAction* GetSwitchAction() const { return SwitchAction; }

	/** Set bullet count (used for checkpoint restore, and by a pickup granting a partial magazine) */
	void SetBulletCount(int32 NewCount);

	/** Tell the owning client what its ammo actually is.
	 *
	 *  Rounds are spent by whichever machine pulls the trigger, so the server's count never moves
	 *  for a client's shots and replicating it continuously would keep overwriting the client's
	 *  correct number with a stale one. What DOES need to cross is the state the server alone
	 *  decides: the magazine a pickup was granted with, and whether that weapon has finite ammo at
	 *  all. Without it a yanked weapon read as an endless magazine on the client while the server
	 *  had counted out forty rounds. Sent on change, not per frame. */
	UFUNCTION(Client, Reliable)
	void Client_SyncAmmoState(int32 InBullets, bool bInHasLimitedAmmo);

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsHitscan() const { return bUseHitscan; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Projectile")
	TSubclassOf<AShooterProjectile> GetProjectileClass() const { return ProjectileClass; }

	UFUNCTION(BlueprintCallable, Category = "Weapon|Projectile")
	void SetProjectileClass(TSubclassOf<AShooterProjectile> NewProjectileClass) { ProjectileClass = NewProjectileClass; }

	/** True if the weapon keeps re-firing while StartFiring is held (continuous auto fire) */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsFullAuto() const { return bFullAuto; }

	/** Set the external refire-interval multiplier (1.0 = normal, <1.0 = faster, >1.0 = slower).
	 *  Clamped to a sane floor so it can never schedule a zero/negative interval. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void SetExternalFireRateMultiplier(float Multiplier) { ExternalFireRateMultiplier = FMath::Max(0.05f, Multiplier); }

	/** Current external refire-interval multiplier. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetExternalFireRateMultiplier() const { return ExternalFireRateMultiplier; }

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

	/** Returns the current fire rate multiplier based on heat (1.0 = normal, higher = slower) */
	UFUNCTION(BlueprintPure, Category = "Weapon|Heat")
	float GetHeatFireRateMultiplier() const { return CalculateHeatFireRateMultiplier(); }

	/** Returns the actual refire rate adjusted for heat */
	UFUNCTION(BlueprintPure, Category = "Weapon|Heat")
	float GetActualRefireRate() const { return GetCurrentRefireRate(); }

	// ==================== Z-Factor Getters ====================

	/** ÃƒÂÃ¢â‚¬â„¢ÃƒÂÃ‚Â¾ÃƒÂÃ‚Â·ÃƒÂÃ‚Â²Ãƒâ€˜Ã¢â€šÂ¬ÃƒÂÃ‚Â°Ãƒâ€˜Ã¢â‚¬Â°ÃƒÂÃ‚Â°ÃƒÂÃ‚ÂµÃƒâ€˜Ã¢â‚¬Å¡ true ÃƒÂÃ‚ÂµÃƒâ€˜Ã‚ÂÃƒÂÃ‚Â»ÃƒÂÃ‚Â¸ Z-Ãƒâ€˜Ã¢â‚¬Å¾ÃƒÂÃ‚Â°ÃƒÂÃ‚ÂºÃƒâ€˜Ã¢â‚¬Å¡ÃƒÂÃ‚Â¾Ãƒâ€˜Ã¢â€šÂ¬ ÃƒÂÃ‚Â²ÃƒÂÃ‚ÂºÃƒÂÃ‚Â»Ãƒâ€˜Ã…Â½Ãƒâ€˜Ã¢â‚¬Â¡ÃƒÂÃ‚ÂµÃƒÂÃ‚Â½ */
	UFUNCTION(BlueprintPure, Category = "Weapon|ZFactor")
	bool IsZFactorEnabled() const { return bUseZFactor; }

	// ==================== ADS Getters ====================

	UFUNCTION(BlueprintPure, Category = "Weapon|ADS")
	bool HasCustomADSOffset() const { return bUseCustomADSOffset; }

	UFUNCTION(BlueprintPure, Category = "Weapon|ADS")
	FVector GetADSOffset() const { return CustomADSOffset; }

	/** How many times the sights magnify. See ADSZoom. */
	UFUNCTION(BlueprintPure, Category = "Weapon|ADS")
	float GetADSZoom() const { return ADSZoom; }

	/** Apply a magnification to a field of view and get the zoomed-in field of view back.
	 *
	 *  Zoom is a ratio of TANGENTS, not of angles, because that is what the projection does:
	 *  on-screen size goes as 1/tan(FOV/2). Halving the angle is NOT 2x zoom. Every place that
	 *  turns ADSZoom into a real FOV goes through here, so there is exactly one copy of this. */
	UFUNCTION(BlueprintPure, Category = "Weapon|ADS")
	static float ApplyZoomToFOV(float BaseFOVDegrees, float Zoom);

	/** Returns ADS blend in time */
	UFUNCTION(BlueprintPure, Category = "Weapon|ADS")
	float GetADSBlendInTime() const { return ADSBlendInTime; }

	/** Returns ADS blend out time */
	UFUNCTION(BlueprintPure, Category = "Weapon|ADS")
	float GetADSBlendOutTime() const { return ADSBlendOutTime; }

	// ==================== ADS Camera ====================

	/** Returns the ADS camera component (used for SetViewTarget blending) */
	UFUNCTION(BlueprintPure, Category = "Weapon|ADS")
	UCameraComponent* GetADSCamera() const { return ADSCameraComponent; }

	/** Whether ADS should match the sight's AXIS to the camera, not only its position.
	 *  See bAlignSightRotation for why this is per weapon rather than a global switch. */
	UFUNCTION(BlueprintPure, Category = "ADS")
	bool ShouldAlignSightRotation() const { return bAlignSightRotation; }

	/** Correction folded into the sight socket's rotation before ADS alignment uses it. */
	UFUNCTION(BlueprintPure, Category = "ADS")
	FRotator GetSightRotationOffset() const { return SightRotationOffset; }

	/** Eye position relative to the sight socket, in camera axes. See SightAimOffset. */
	UFUNCTION(BlueprintPure, Category = "ADS")
	FVector GetSightAimOffset() const { return SightAimOffset; }

public:
	// ==================== Recoil Getters ====================

	UFUNCTION(BlueprintPure, Category = "Weapon|Recoil")
	bool UsesAdvancedRecoil() const { return bUseAdvancedRecoil; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Recoil")
	const FWeaponRecoilSettings& GetRecoilSettings() const { return RecoilSettings; }

	// ==================== Crosshair ====================

	/** Per-weapon crosshair appearance (read by the HUD crosshair widget). */
	UFUNCTION(BlueprintPure, Category = "Weapon|Crosshair")
	const FCrosshairConfig& GetCrosshairConfig() const { return CrosshairConfig; }

	/** True while the trigger is held / the weapon is firing. Drives the cosmetic crosshair bloom
	 *  (grow-on-fire). */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsFiring() const { return bIsFiring; }

	// ==================== Spread Getters ====================

	/** The spread right now, in degrees: the half-angle of the cone a shot may leave in.
	 *  clamp(AimVariance * state multiplier + firing bloom, 0, MaxSpreadDegrees).
	 *  This is the single number the bullets and the crosshair both read, which is what keeps them
	 *  from disagreeing. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Spread")
	float GetCurrentSpreadDegrees() const;

	/** How far the AIM LINE itself is allowed to wander, in degrees. The same as
	 *  GetCurrentSpreadDegrees for an ordinary weapon; a shotgun overrides it to zero and spends
	 *  the spread on widening its pattern instead, so the triangle stays a triangle. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Spread")
	virtual float GetAimConeDegrees() const { return GetCurrentSpreadDegrees(); }

	/** The angle the crosshair should draw as its radius. Same as GetCurrentSpreadDegrees for an
	 *  ordinary weapon; a shotgun returns the outer edge of its pellet pattern, because that, and
	 *  not the wander of the aim line, is the region its shot covers. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Spread")
	virtual float GetCrosshairSpreadDegrees() const { return GetCurrentSpreadDegrees(); }

	/** The per-weapon spread tuning (read by the HUD crosshair widget). */
	const FWeaponSpreadConfig& GetSpreadConfig() const { return SpreadConfig; }

	// ==================== Hitscan Getters ====================

	float GetHitscanDamage() const { return HitscanDamage; }
};
