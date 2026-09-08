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
#include "GameplayTagContainer.h"
#include "Engine/NetSerialization.h"
// For EWeaponAttachmentType. Included rather than forward declared because UHT needs the complete
// enum to build the TMap property below.
#include "Variant_Shooter/Weapons/WeaponAttachmentDefinition.h"
#include "ShooterWeapon.generated.h"

class IShooterWeaponHolder;
class AShooterProjectile;
class URecoilData;
class UPrimaryDataAsset;
class USkeletalMeshComponent;
class UCameraComponent;
class UAnimMontage;
class UAnimInstance;
class UAnimationAsset;
class UNiagaraSystem;
class UNiagaraComponent;
class UPhysicalMaterial;
class UTexture2D;
class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;
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

	/** Which hotkey slot this INSTANCE sits in on its current owner, or INDEX_NONE when nobody has
	 *  placed it.
	 *
	 *  The slot belongs to the PLAYER, not to the weapon class: the class weapon is always slot 0
	 *  (key 1) and a looted one always slot 1 (key 2), so the same gun answers to key 1 in the hands
	 *  of the class that starts with it and to key 2 when it is picked up off the ground. That is
	 *  why this is an instance field the character writes rather than a default on the Blueprint.
	 *
	 *  Written on every path that puts a weapon into AShooterCharacter::OwnedWeapons; replicated
	 *  because the key is pressed on the owning client, which is looking at a replicated copy of
	 *  that array. SwitchAction above stays as the fallback for weapons nobody placed (NPC guns). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Input")
	int32 HotkeySlot = INDEX_NONE;

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

	// ==================== What a shot of this weapon is worth ====================
	//
	// THE SPLIT, and the rule that decides which side a field belongs on:
	//
	//   The WEAPON owns what a SHOT is worth -- damage, headshot, damage type, ionization, tag
	//   multipliers, the shield gate. These are balance numbers for the gun, and they apply the
	//   same whether the shot is carried by a trace, a bolt, or a projectile actor. They are NOT
	//   under EditCondition "bUseHitscan" any more: they never were hitscan-only in the code, only
	//   in the editor, so a projectile weapon's damage sat greyed out at 20 while the round it
	//   fired silently inherited it.
	//
	//   The PROJECTILE owns what the ROUND is -- how fast it flies, whether it falls, whether it
	//   bounces or homes, what it does on impact (explosion, radius, falloff, rocket jump, physics
	//   force, noise), and how long it lives. None of that has a meaning for a trace.
	//
	//   A projectile may OVERRIDE any of the weapon's shot numbers, and a special payload should:
	//   that is how a rocket stays a rocket after Upgrade_RocketProjectileSwap loads a different
	//   round into the same tube. Overrides are read off the projectile CDO by GetShotPayload()
	//   below, and only when the weapon actually fires projectiles.
	//
	// Genuinely hitscan-only settings (range, the bolt, divergence, reflection, the wave) keep their
	// EditCondition and stay in the Hitscan category.

	/** Damage one shot of this weapon does, BEFORE the headshot and every situational multiplier.
	 *
	 *  THE damage number for this weapon in both firing modes. The name is the only thing left over
	 *  from when it meant "damage while tracing"; the projectile path has read it through
	 *  GetShotDamage since the round stopped carrying its own. A projectile class may still override
	 *  it (AShooterProjectile::HitDamage >= 0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (DisplayName = "Shot Damage", ClampMin = "0"))
	float HitscanDamage = 20.0f;

	/** Multiplier when the shot lands on the head bone. Applies to traces, bolts and projectiles
	 *  alike -- ApplyWeaponHit resolves the bone for all three. A projectile may override it
	 *  (AShooterProjectile::HeadshotMultiplierOverride >= 0), which is how a rocket stops caring
	 *  where on a body it went off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "1.0"))
	float HeadshotMultiplier = 2.0f;

	/** Damage type for a shot of this weapon. A projectile with its own HitDamageType wins, because
	 *  fire and explosion belong to the payload rather than to the barrel it left. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (DisplayName = "Damage Type"))
	TSubclassOf<UDamageType> HitscanDamageType;

	// ==================== Hitscan-only Settings ====================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitscan", meta = (EditCondition = "bUseHitscan", ClampMin = "0"))
	float MaxHitscanRange = 10000.0f;

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

	/** This weapon may hurt whoever is holding it. Read by ApplyWeaponHit for every carrier, so it
	 *  covers a trace that came back at the shooter as well as a round that did. A projectile that
	 *  is SUPPOSED to hurt its owner (a rocket at your own feet) says so itself with bDamageOwner
	 *  and is not silenced by this being off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (DisplayName = "Can Damage Owner"))
	bool bHitscanDamageOwner = false;

	/** Damage multipliers based on target actor tags. Multiple matching tags multiply together.
	 *  Folded into GetShotDamageMultiplierAgainst, which every carrier goes through, so this is the
	 *  gun's answer for traces and rounds alike. A gunless projectile falls back to its own copy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
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

	// ==================== Ionization ====================
	//
	// Not hitscan-only, and it never was in the code: ApplyWeaponHit ionizes for every carrier, so a
	// projectile weapon has been electrifying its targets all along while these three fields sat
	// greyed out in the editor and could not be tuned. A projectile may override the per-hit amount,
	// or refuse to ionize at all -- see AShooterProjectile::IonizationOverride.

	/** If true, a hit from this weapon applies a fixed charge to the target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Ionization", meta = (DisplayName = "Use Ionization"))
	bool bUseHitscanIonization = false;

	/** Charge added to the target per hit. Signed: negative electrifies the other way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Ionization", meta = (EditCondition = "bUseHitscanIonization"))
	float IonizationChargePerHit = 2.0f;

	/** Maximum charge MAGNITUDE ionization can drive a target to (also used by laser) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Ionization", meta = (ClampMin = "0.0", ClampMax = "100.0"))
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

	/** Default impact VFX, used when the surface has no PhysicalMaterial or is missing from
	 *  ImpactFXBySurface. Used by BOTH firing modes: the shot that landed is the shot that landed,
	 *  whatever carried it there. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Impact")
	TObjectPtr<UNiagaraSystem> ImpactFX;

	/** Per-surface impact VFX. Key is the SurfaceType configured in Project Settings -> Physics -> Physical Surfaces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Impact")
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
	//
	// Not hitscan-only. SAME mistake as the damage and ionization fields above, found the same way:
	// AShooterProjectile::NotifyHit has always called AShooterWeapon::SpawnImpactEffect, so a
	// projectile weapon has been asking for an impact all along -- and getting nothing, because the
	// editor greyed these out and nobody could fill them in. No blood, no chips off the brick, and
	// nothing anywhere saying why.

	/** Default impact sound (used when surface has no PhysicalMaterial or is missing from ImpactSoundBySurface) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Impact")
	TObjectPtr<USoundBase> DefaultImpactSound;

	/** Per-surface impact sound. Key is the SurfaceType configured in Project Settings -> Physics -> Physical Surfaces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Impact")
	TMap<TEnumAsByte<EPhysicalSurface>, TObjectPtr<USoundBase>> ImpactSoundBySurface;

	/** Optional attenuation for impact sounds (3D spatialization, falloff) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Impact")
	TObjectPtr<USoundAttenuation> ImpactSoundAttenuation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Impact", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float ImpactSoundPitchMin = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Impact", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float ImpactSoundPitchMax = 1.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Impact", meta = (ClampMin = "0.0", ClampMax = "2.0"))
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

	/** The holder works the action after a shot: a bolt, a pump, a lever.
	 *
	 *  Setting this is what MAKES a weapon manual action. Firing is gated until the animation is
	 *  over (GetCurrentRefireRate takes its length as a floor), so the rate of fire of such a gun is
	 *  the length of this animation and not a number typed next to it. That is the honest way round:
	 *  a bolt rifle that could fire before the bolt was closed would be lying about what it shows.
	 *
	 *  Deliberately NOT FiringMontage, which every weapon in the project may already use for a
	 *  flourish. Overloading that one would gate twenty weapons that were never meant to be gated. */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TObjectPtr<UAnimMontage> CycleActionMontage;

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

	/** Played on the weapon's own meshes when a reload starts. Primary slot: the empty reload. */
	UPROPERTY(EditAnywhere, Category = "Animation|Weapon Mesh")
	TObjectPtr<UAnimationAsset> WeaponMeshReloadAnimation;

	/** The weapon's half of the second reload. Empty falls back to WeaponMeshReloadAnimation. */
	UPROPERTY(EditAnywhere, Category = "Animation|Weapon Mesh")
	TObjectPtr<UAnimationAsset> WeaponMeshSecondaryReloadAnimation;

	/** The weapon's half of the closing stage of a per round reload. */
	UPROPERTY(EditAnywhere, Category = "Animation|Weapon Mesh")
	TObjectPtr<UAnimationAsset> WeaponMeshReloadEndAnimation;

	/** Played instead of WeaponMeshFireAnimation on the shot that EMPTIES the magazine.
	 *
	 *  This is the slide or the bolt staying open, and it is a different animation rather than the
	 *  same one stopped early: the ordinary fire animation returns to battery, so a gun with an
	 *  empty magazine would sit there looking loaded. Only weapons whose action actually locks back
	 *  have one, and empty means every shot uses the ordinary animation. */
	UPROPERTY(EditAnywhere, Category = "Animation|Weapon Mesh")
	TObjectPtr<UAnimationAsset> WeaponMeshLastShotAnimation;

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

	// ==================== FPS Animation Pack profile ====================
	//
	// One pointer that carries a whole weapon. Their DA_<gun> (a WeaponSettings_C data asset) holds
	// the fire rate, the magazine, the fire sound, the PRAS recoil asset, the camera shake, the
	// weapon mesh's anim blueprint, the montages for the weapon mesh, AND a nested ViewmodelSettings
	// asset with the montages for the hands. Assigning it plus the mesh reproduces every single
	// asset that was set by hand on BP_AR, verified field by field on DA_MX16A4.
	//
	// NOTHING here is required. An empty profile leaves every field below exactly as the Blueprint
	// set it, which is what keeps the twenty-odd weapons that predate the pack working untouched.

	/** Their DA_<weapon> (WeaponSettings_C). Empty = this weapon is not a pack weapon and every
	 *  field is read from the Blueprint as before.
	 *
	 *  Typed as UPrimaryDataAsset rather than the real class because the real class is Blueprint
	 *  only: WeaponSettings_C has no native parent to cast to, so it is read by property name. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS Animation Pack")
	TObjectPtr<UPrimaryDataAsset> PackWeaponSettings;

	/** Recoil asset, when it should differ from the one inside PackWeaponSettings, or when a weapon
	 *  wants PRAS recoil without taking the rest of the profile. Empty falls through to the
	 *  profile's own RecoilSettings; both empty means this weapon keeps OUR recoil. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS Animation Pack")
	TObjectPtr<URecoilData> PackRecoilData;

	/** Amplitude of the pack's WEAPON recoil, as a multiplier on everything PRAS moves (pitch,
	 *  kickback, yaw, roll, noise).
	 *
	 *  Goes through their own ScaleInput, so tuning a weapon down never edits a shared recoil asset
	 *  that eight other guns also use. 1 is the pack author's own numbers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS Animation Pack",
		meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float PackRecoilScale = 1.0f;

	/** Amplitude of the pack's CAMERA jolt, the curve shake their DA calls RecoilShake.
	 *
	 *  Separate from PackRecoilScale on purpose: how hard the gun jumps and how hard the view is
	 *  punched are two different tastes, and the pack ships them as two different assets. 0 turns
	 *  the jolt off and leaves the smooth ControllerRecoil climb alone. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS Animation Pack",
		meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float PackCameraShakeScale = 1.0f;

	/** Escape hatch for the rate of fire: off keeps the Blueprint's own RefireRate even under a
	 *  profile. Defaults to on, so out of the box the profile decides like everything else.
	 *
	 *  Exists only because a number has no "unset" the way an asset pointer does, so a weapon that
	 *  wants their animations but its own pacing has no other way to say so. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS Animation Pack",
		meta = (EditCondition = "PackWeaponSettings != nullptr"))
	bool bPackSetsFireRate = true;

	/** Same, for the magazine (their Ammo). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS Animation Pack",
		meta = (EditCondition = "PackWeaponSettings != nullptr"))
	bool bPackSetsMagazine = true;

	/** Reads PackWeaponSettings over the fields below.
	 *
	 *  THE PROFILE WINS. Setting PackWeaponSettings is the statement "this weapon is theirs, set it
	 *  up the way its author did", so every value the profile carries replaces ours. Our own
	 *  montage, sound and number fields are the fallback for a weapon with NO profile, which is
	 *  every weapon that predates the pack.
	 *
	 *  A slot the profile leaves empty is not an instruction to clear: MX16A4 has no hands Fire
	 *  montage because PRAS does that shake, and blanking ours over it would take away a working
	 *  animation to replace it with nothing.
	 *
	 *  Called once from BeginPlay, before anything reads a montage. Every value taken is logged
	 *  under [PACK] so a weapon that behaves oddly can be traced to the field it inherited rather
	 *  than to the field somebody thought they set. */
	void ApplyPackWeaponSettings();

	/** Hands this weapon's profile to the pack's ViewmodelController on the holder, so its anim
	 *  graph poses the arms for THIS gun.
	 *
	 *  Not cosmetic bookkeeping: ActiveSettings is where their stack reads the hold pose from, and
	 *  while it stayed pinned to whatever the character Blueprint was saved with, every weapon in
	 *  the game was held in that one weapon's pose. A revolver got a rifle's grip, and every reload
	 *  animation snapped back to the rifle hold the moment it ended, because the montage and the
	 *  pose underneath it belonged to two different guns.
	 *
	 *  Called on equip. A weapon with no profile writes nothing at all rather than clearing: our own
	 *  weapons run our own arms graph and have no pack pose to offer, and blanking the field is how
	 *  the hands end up in the skeleton's base pose with the wrists a metre apart. */
	void PushPackViewmodelSettings();

	// The two questions the CHARACTER asks about this weapon's recoil, so both are public. The
	// fields and the filling above stay protected: they are the weapon's own business.
public:

	/** The PRAS recoil asset this weapon should drive, or null when it has none.
	 *
	 *  Null is the signal that OUR WeaponRecoilComponent stays in charge, so this is the one
	 *  question the character asks to decide which recoil system runs. */
	UFUNCTION(BlueprintPure, Category = "FPS Animation Pack")
	URecoilData* GetPackRecoilData() const { return ResolvedPackRecoilData; }

	/** Rounds per minute for PRAS, which wants a rate rather than an interval. Taken from the
	 *  profile when it has one, otherwise derived from our own RefireRate so a weapon with a hand
	 *  assigned PackRecoilData and no profile still initialises correctly. */
	UFUNCTION(BlueprintPure, Category = "FPS Animation Pack")
	float GetPackFireRateRPM() const;

	/** The camera jolt this weapon fires, or null when it has none. Null is the whole test: no
	 *  curve means no jolt, and the smooth ControllerRecoil climb is unaffected either way. */
	UCurveVector* GetPackShakeCurve() const { return PackShakeCurve; }

	float GetPackShakePlayRate() const { return PackShakePlayRate; }
	float GetPackShakeSmoothing() const { return PackShakeSmoothing; }
	float GetPackCameraShakeScale() const { return PackCameraShakeScale; }
	float GetPackRecoilScale() const { return PackRecoilScale; }

	/** One shot's worth of jolt, in degrees, already scaled by PackCameraShakeScale.
	 *
	 *  Rolled here rather than by the caller so the ranges stay the weapon's business, and rolled
	 *  ONCE PER SHOT rather than per frame: sampling the random range every tick would turn a
	 *  directed kick into noise. */
	FRotator RollPackShakeAmplitude() const;

protected:

	/** Resolved once in ApplyPackWeaponSettings: the override if given, else the profile's own.
	 *  Kept rather than re-derived because it is asked once per shot. */
	UPROPERTY(Transient)
	TObjectPtr<URecoilData> ResolvedPackRecoilData;

	/** Rounds per minute read out of the profile, 0 when it had none. */
	UPROPERTY(Transient)
	float PackFireRateRPM = 0.0f;

	// The camera jolt, unpacked once at BeginPlay from the profile's RecoilShake asset. Held as
	// three plain values rather than as the asset, because the asset is a Blueprint class
	// (CameraRecoilShake_C) with no native type to hold, and re-reading it by property name every
	// shot would be reflection in the hot path for numbers that never change.
	UPROPERTY(Transient)
	TObjectPtr<UCurveVector> PackShakeCurve;

	UPROPERTY(Transient)
	float PackShakePlayRate = 1.0f;

	UPROPERTY(Transient)
	float PackShakeSmoothing = 55.0f;

	// Degrees, as a MIN..MAX range per axis, exactly as the pack stores them. The curve above is a
	// normalised shape peaking at 1; these are what turn it into an angle, and they are rolled
	// fresh on every shot so a burst does not repeat the same jolt.
	UPROPERTY(Transient)
	FVector2D PackShakePitchRange = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	FVector2D PackShakeYawRange = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	FVector2D PackShakeRollRange = FVector2D::ZeroVector;

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

	/** The FPS Animation Pack's name for the same thing ADSSocketName means, checked right after it.
	 *
	 *  Every one of their twenty weapon meshes carries exactly this socket and no other eye point:
	 *  SKM_MX16A4_New has two sockets in total, AimPoint and Ejector. Their BP_WeaponBase.OnEquipped
	 *  reads it with GetSocketTransform(RTS_Component) and that IS their whole aiming input, so a
	 *  weapon of theirs must land here or the pack's aiming layer gets a number from somewhere else
	 *  entirely — which is what "the gun flies over your head while aiming" looked like.
	 *
	 *  Deliberately a separate name rather than a changed default for ADSSocketName: our own weapons
	 *  say "Sight", theirs say "AimPoint", and both sets have to keep working off one chain. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	FName PackAimSocketName = FName("AimPoint");

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

	// ==================== Attachments ====================
	//
	// A socket belongs to the mesh it was authored on, and that mesh is this weapon's. Two rifles
	// mount the same scope on sockets with different names, so the map from type to socket lives
	// here rather than on the attachment asset. The attachment carries exactly one socket of its
	// own -- SOCKET_Aim, the eye point -- and that one does travel with it.
	//
	// A missing socket is a REFUSAL, never a fallback to the component origin: an attachment
	// mounted at the origin sits inside the gun, which reads as a broken mesh rather than as a
	// missing socket and costs a session to work out.

	/** Where each kind of attachment goes on this weapon's meshes. A type that is absent falls back
	 *  to ScopeMountSocketName for the optic (it is the same rail) and to nothing for the rest,
	 *  which refuses the mount and says so in the log.
	 *
	 *  Third person uses the same names with a "_TP" suffix when the artist authored one, the same
	 *  rule PickThirdPersonSocket uses for the grip; without a _TP variant both meshes use the same
	 *  socket name. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachments")
	TMap<EWeaponAttachmentType, FName> AttachmentSockets;

	/** Name of a component in this weapon's Blueprint that IS the built-in sight, when the weapon
	 *  carries one as its own component rather than as part of the mesh.
	 *
	 *  It exists for exactly one reason. ResolveADSAnchorAttachment finds the eye point by looking
	 *  for SOCKET_Aim anywhere under the weapon mesh, and a weapon whose default scope is its own
	 *  component has that socket too. Mount a real optic and there are suddenly TWO candidates,
	 *  with child order deciding which one wins -- which is not a decision anybody made. Naming the
	 *  built-in one here lets it be hidden and skipped while a real optic is mounted.
	 *
	 *  Leave empty for a weapon whose iron sights are part of the mesh. That is the common case and
	 *  it needs nothing: with no optic mounted the anchor falls through to ADSSocketName by itself. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachments")
	FName DefaultOpticComponentName;

	/** What is mounted right now, in mount order.
	 *
	 *  Replicated because a scope is not a private fact: the other three players see this gun in
	 *  third person and have to see what is bolted to it. Every machine rebuilds its own meshes
	 *  from OnRep, so no RPC carries the mesh itself. */
	UPROPERTY(ReplicatedUsing = OnRep_InstalledAttachments)
	TArray<TObjectPtr<UWeaponAttachmentDefinition>> InstalledAttachments;

	UFUNCTION()
	void OnRep_InstalledAttachments();

	/** Destroy the mounted meshes and build them again from InstalledAttachments. Runs on every
	 *  machine, from OnRep and from the server's own write, so both see the same gun. */
	void RebuildAttachmentMeshes();

	/** The socket this type mounts on for the given mesh, or NAME_None when there is not one.
	 *
	 *  NAME_None is a real answer and callers must treat it as a refusal. Attaching to a component
	 *  with no socket silently lands the part at the component's own origin, which puts a scope
	 *  inside the receiver: it looks like a broken mesh rather than a missing socket. */
	FName ResolveAttachmentSocket(EWeaponAttachmentType InType, const USkeletalMeshComponent* Mesh) const;

	/** Inherit render visibility and late tick onto everything under the weapon meshes. Called at
	 *  BeginPlay and again after anything is mounted: a component created at runtime starts with
	 *  the defaults, so without this a mounted scope draws in the world pass while the gun draws in
	 *  the first person pass, and reads its socket a frame late. */
	void ApplyChildComponentSetup();

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

	/** A real magazine fed by an endless reserve: the gun runs dry and has to be reloaded, but the
	 *  rounds it reloads with come from nowhere and cost no inventory cells.
	 *
	 *  This is what the starting weapon is. The two existing behaviours could not express it: with
	 *  bUseReload off there is no magazine to run out of, and with it on the reserve lives in the
	 *  grid, which nothing ever fills for a weapon that was granted rather than looted -- so the
	 *  first magazine was also the last. A looted gun leaves this off and stays on the cell economy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo|Reload", meta = (EditCondition = "bUseReload"))
	bool bInfiniteReserve = false;

	/** How long the magazine takes to fill. Match it to the reload montage, or the weapon fires out
	 *  of an animation that has not finished. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo|Reload", meta = (EditCondition = "bUseReload", ClampMin = "0.05", Units = "s"))
	float ReloadTime = 2.0f;

	// ==================== The second reload ====================
	//
	// A weapon that ran dry has to work the bolt; one with a round still chambered does not, and is
	// noticeably quicker. The FPS Animation Pack ships both animations for every weapon it has, and
	// the fields below are the second of the pair.
	//
	// The slots are named after THEIR slots (Primary / Secondary) rather than after what this
	// weapon uses them for. That is not vagueness: the pack puts a different pair in the same two
	// slots depending on the weapon. A rifle keeps empty and tactical there; a shotgun and a bolt
	// action keep the start and the loop of a shell-by-shell reload, and use a third slot for its
	// end. Naming these "Tactical" would have to be undone the moment a pump gun is migrated.
	//
	// Everything here is optional. Left empty, the weapon reloads exactly as it always has, which
	// is what every weapon in the project that has not been migrated needs.

	/** Seconds the second reload takes. Zero measures the montage instead, which is the answer you
	 *  want: a hand-typed duration that drifts from the animation is precisely how a weapon ends up
	 *  firing out of a reload that has not finished. Zero with no montage falls back to ReloadTime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo|Reload", meta = (EditCondition = "bUseReload", ClampMin = "0.0", Units = "s"))
	float SecondaryReloadTime = 0.0f;

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

	/** Played on the holder for the duration of the reload, first and third person both. This is
	 *  the PRIMARY slot: the reload with an empty magazine. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo|Reload", meta = (EditCondition = "bUseReload"))
	TObjectPtr<UAnimMontage> ReloadMontage;

	/** The holder's half of the second reload. Empty falls back to ReloadMontage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo|Reload", meta = (EditCondition = "bUseReload"))
	TObjectPtr<UAnimMontage> SecondaryReloadMontage;

	// ==================== Third reload shape: one round at a time ====================
	//
	// A pump gun and a bolt rifle do not swap a magazine, they push rounds in one by one, and the
	// animation is three assets rather than one: an opening, a loop played once per round, and a
	// closing. The two slots above are reused rather than duplicated, because that is how the pack
	// itself stores them and duplicating them would mean two sets of fields meaning the same thing:
	//
	//     ReloadMontage           -> the opening   (their PrimaryReload)
	//     SecondaryReloadMontage  -> the loop      (their SecondaryReload)
	//     ReloadEndMontage        -> the closing   (their AdditionalReload)
	//
	// That reuse is exactly why UsesSecondaryReload must never be consulted on such a weapon: for
	// a magazine gun it answers "is there a round in the chamber", and here the same two assets
	// mean something else entirely.

	/** Rounds go in one at a time. Set from the pack profile: their WeaponClass says BP_ManualAction
	 *  for precisely the two guns built this way, so the shape is read from their data rather than
	 *  ticked by hand per weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo|Reload", meta = (EditCondition = "bUseReload"))
	bool bPerRoundReload = false;

	/** The holder's closing animation for a per round reload. Empty means the reload simply ends
	 *  after the last round, with no separate closing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo|Reload", meta = (EditCondition = "bPerRoundReload"))
	TObjectPtr<UAnimMontage> ReloadEndMontage;

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

	// ==================== Per round reload state ====================
	//
	// The magazine reload is one timer and one animation, so it needs no state beyond bIsReloading.
	// This one is a loop, and a loop that the player is allowed to walk out of, so it has to know
	// which stage it is in: the closing animation must not play twice, and a round must be credited
	// for each COMPLETED loop and no more.

	/** Which stage of a per round reload is on screen. Only meaningful while bIsReloading. */
	EWeaponReloadStage ShellStage = EWeaponReloadStage::Primary;

	/** Opens a per round reload: plays the opening, then hands over to AdvancePerRoundReload. */
	void BeginPerRoundReload();

	/** One step of the loop. Credits the round the previous loop just seated, then either seats
	 *  another or closes the reload. Timer driven, so it is also the only place that decides when a
	 *  per round reload is finished. */
	void AdvancePerRoundReload();

	/** Plays the closing animation and schedules FinishReload behind it. */
	void EndPerRoundReload();

	/** Drop a per round reload where it stands and keep every round already seated.
	 *
	 *  This is what makes the reload interruptible, and interruptible is the whole point of loading
	 *  one round at a time: the player takes the shot with three in the tube rather than watching
	 *  the animation finish first. Nothing is refunded and nothing is lost, because each round was
	 *  credited when its own loop ended. */
	void InterruptPerRoundReload();

	/** Plays one stage of a reload on this machine and, when this machine is allowed to, on every
	 *  other. The single place that knows a stage maps to two assets, the arms and the gun. */
	void PlayReloadStage(EWeaponReloadStage Stage);

	// ==================== Refire ====================

	UPROPERTY(EditAnywhere, Category = "Refire", meta = (ClampMin = 0, ClampMax = 10, Units = "s"))
	float RefireRate = 0.1f;

	/** Legacy scalar pitch kick, used only when bUseAdvancedRecoil is false. Dead once advanced
	 *  recoil is on — the pattern curve in RecoilSettings.Pattern owns the kick then. */
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
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Meshes created for the mounted attachments, on both weapon meshes. Owned here so a rebuild
	 *  can destroy exactly what it made and nothing a Blueprint added by hand. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> AttachmentMeshComponents;

	/** The first person mesh of the mounted OPTIC, when there is one.
	 *
	 *  Kept as an explicit pointer so the ADS anchor never has to guess. The generic search for
	 *  SOCKET_Aim walks the whole subtree in child order, and on a weapon that carries a built-in
	 *  scope component there would be two matches with nothing but that order to separate them.
	 *  This one is checked first, so the answer does not depend on which component was made when. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> MountedOpticMesh;

	/** The weapon's own built-in sight component, found once from DefaultOpticComponentName. Hidden
	 *  while a real optic is mounted, and skipped by the eye-point search for the same reason. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> DefaultOpticComponent;

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

	/** True when the magazine is real but the reserve behind it is endless. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Reload")
	bool HasInfiniteReserve() const { return bUseReload && bInfiniteReserve; }

	/** True when this weapon's rounds are carried in the inventory grid, which is the one question
	 *  every ammo-economy caller actually asks. Both ways of being infinite answer it the same way,
	 *  so callers test this rather than UsesReload and there is no second rule to keep in step. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Reload")
	bool OwnsAmmoCells() const { return bUseReload && !bInfiniteReserve; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Reload")
	float GetReloadTime() const { return ReloadTime; }

	/** Seconds the reload that would start now will take. Prefers an explicit override, then the
	 *  length of the montage that will actually play, and only then the legacy ReloadTime. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Reload")
	float GetActiveReloadTime() const;

	/** The montage this weapon plays while reloading, if it has one. The PRIMARY one: callers that
	 *  need whichever is actually running want GetActiveReloadMontage instead. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Reload")
	UAnimMontage* GetReloadMontage() const { return ReloadMontage; }

	/** True when there is still a round in the chamber, so this reload skips working the bolt.
	 *  Read before the magazine is refilled, which is why it is stable for a whole reload. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Reload")
	bool UsesSecondaryReload() const { return CurrentBullets > 0; }

	/** Whichever of the two reload montages this weapon would play right now.
	 *
	 *  A per round weapon always answers with the OPENING: it is the only stage that exists at the
	 *  moment a reload is asked for, and the rest of the sequence is chosen a stage at a time as it
	 *  runs. Answering with the loop here would hand the caller a fraction of a second and the gun
	 *  would consider itself reloaded after one shell. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Reload")
	UAnimMontage* GetActiveReloadMontage() const
	{
		if (bPerRoundReload)
		{
			return ReloadMontage;
		}

		return (UsesSecondaryReload() && SecondaryReloadMontage) ? SecondaryReloadMontage : ReloadMontage;
	}

	/** Rounds go in one at a time, so the reload is a loop and can be walked out of. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Reload")
	bool IsPerRoundReload() const { return bPerRoundReload; }

	/** Seconds the manual action takes, or 0 on a weapon that has none. Read as a FLOOR on the
	 *  refire interval, which is what stops a bolt rifle firing through its own bolt. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Refire")
	float GetCycleActionSeconds() const;

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

	// ==================== AI lead ====================

	/** How much of the target's own speed an AI anticipates, 0 to 1.
	 *
	 *  Deliberately below one. A shooter that solves the lead exactly hits a sprinting player every
	 *  time at any range, and the answer to it is to stop moving - which inverts the whole game,
	 *  because movement is what this project is about. At 0.7 a still target is dead, a walking one
	 *  is in danger, and a sprinting one is usually missed by the width of a stride.
	 *
	 *  Horizontal only. Vertical velocity is jumps and falls, it reverses inside the flight time,
	 *  and leading it aims the shot into the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|AI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AILeadFraction = 0.7f;

	/** Scatter added to the lead, as a fraction of the lead itself.
	 *
	 *  Proportional on purpose: the error grows with the target's speed, so it never turns a shot
	 *  at a standing man into a miss, and it makes running work.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|AI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AILeadErrorFraction = 0.15f;

	/** Ceiling on the anticipated flight time. A shot across the whole map would otherwise lead a
	 *  sprinting target by tens of metres, which looks like the AI shooting at nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|AI", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float AIMaxLeadSeconds = 2.0f;

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
	/**
	 * ONE funnel for a shot of this weapon that landed, whatever carried it there: an instant trace,
	 * a travelling bolt, or a projectile actor.
	 *
	 * Everything a hit of this weapon means lives here and nowhere else: foliage conversion, the
	 * shield gate, the class passive's shield-piercing damage, the headshot multiplier, the upgrade
	 * and ability notifications, knockback under the grounded rule, ionization, and the hit marker.
	 * A caller that applies damage itself gets none of it, which is exactly how the projectile
	 * spent its life punching through shields in silence.
	 *
	 * BaseDamage is the damage BEFORE the headshot multiplier and ExtraDamageMultiplier: the caller
	 * owns the number (the weapon's HitscanDamage, the projectile's HitDamage, a charge-scaled one),
	 * the weapon owns the rules applied to it.
	 *
	 * HitDirection points INTO the target and drives both the impulse and the feedback direction.
	 * ImpulseForce is one number for both characters and physics props, as on the hitscan path;
	 * zero skips the push entirely, which is what a caller doing its own knockback wants.
	 * OverrideDamageType null falls back to HitscanDamageType.
	 *
	 * OverrideDamageEvent lets a caller hand over an event it has already built, and an explosion
	 * must: FRadialDamageEvent carries the blast origin and radius, and everything downstream that
	 * asks WHERE a body was hit reads it. Building a plain event over the top would throw that away.
	 * Null means the ordinary point-damage event assembled here.
	 *
	 * Returns the damage that actually landed.
	 */
	float ApplyWeaponHit(const FHitResult& Hit, float BaseDamage, const FVector& HitDirection,
		float ImpulseForce, float ExtraDamageMultiplier = 1.0f,
		TSubclassOf<UDamageType> OverrideDamageType = nullptr,
		bool bAllowOwnerDamage = false,
		const FDamageEvent* OverrideDamageEvent = nullptr);

	/** Hit confirmation for a shooter who is not on this machine.
	 *
	 *  A hitscan shot is resolved on the shooter's own machine, so its marker never needed to travel.
	 *  A projectile is resolved by the authority, and for a client's shot that is somebody else's
	 *  computer: the marker has to be sent back down or the shooter hears nothing at all. Unreliable
	 *  on purpose -- a confirmation that arrives late is worse than one that never arrives. */
	UFUNCTION(Client, Unreliable)
	void Client_ReportHitFeedback(const FHitFeedbackContext& Context);

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

public:
	// The impact trio is public because a projectile lands this weapon's shots too, and it has to
	// spawn the same impact the trace would have: the shooter's local stand-in plays it directly,
	// the authority's copy goes through the networked call.

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

protected:

	UFUNCTION(BlueprintCallable, Category = "VFX")
	void SpawnReflectionEffect(const FVector& Location, const FVector& IncomingDirection, const FVector& ReflectedDirection);

	UFUNCTION(BlueprintCallable, Category = "SFX")
	void PlayFireSound();

	/** Muzzle flash + fire sound on this machine. The shared body of the local call and the
	 *  multicast, so the effects can never drift apart between owner and observers. */
	/** bLastRound picks WeaponMeshLastShotAnimation over the ordinary one, and it is passed in
	 *  rather than read off CurrentBullets because the round has not left the magazine yet when the
	 *  effects play, and on an observer's machine the count is a replicated guess anyway. */
	void PlayFireEffectsLocally(bool bLastRound);

	/** The weapon's own reload on this machine: the moving parts and the sound. Same split as the
	 *  firing effects above, for the same reason. */
	void PlayReloadEffectsLocally(EWeaponReloadStage Stage);

public:
	/** Muzzle flash and fire sound, played on every machine that can see this weapon.
	 *  Cosmetic only, so it is unreliable: a dropped shot effect is better than a stalled channel
	 *  during sustained fire. Multicast originates on the authority, which is why the firing client
	 *  plays its own effects locally first instead of waiting for the round trip.
	 *  Public because the owning character relays a client's shot through it. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayFireEffects(bool bLastRound);

	/** The magazine coming out, the pump, the shells going in, played on every machine that can see
	 *  this weapon. Everyone but the owner only ever has the third person mesh, so without this the
	 *  gun sat perfectly still through a teammate's or an enemy's whole reload.
	 *  Reliable, unlike the firing one: this happens once per magazine rather than once per shot,
	 *  and a dropped one leaves a visibly dead weapon for the entire reload. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayReloadEffects(EWeaponReloadStage Stage);

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

	/**
	 * The round leaving the barrel, for every machine that is not already drawing one.
	 *
	 * This is what replaces replicating the projectile itself. Instead of an actor channel per round
	 * per client fed at the actor default of 100 Hz, one unreliable event of a position and a
	 * direction goes out once, and each client simulates the flight from it. The path is entirely
	 * predictable -- muzzle, direction, the speed and gravity that live on the projectile class --
	 * so a locally simulated copy and the server's copy stay together without being told to.
	 *
	 * The copies it makes are decoration and nothing else: SetCosmeticOnly strips them of the pawn
	 * collision response and CanAffectWorld refuses them everything that touches the game. The
	 * server's own unreplicated round is what lands every hit.
	 *
	 * Unreliable on purpose, exactly like the muzzle flash: a dropped one costs a bullet nobody saw,
	 * and a stalled channel during sustained fire costs the whole fight.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_SpawnCosmeticProjectile(FVector_NetQuantize100 MuzzleLocation,
		FVector_NetQuantizeNormal Direction);

	/**
	 * Fill this weapon's projectile pool before anybody pulls a trigger.
	 *
	 * The pool used to prewarm itself on its first request, which is the worst possible moment: the
	 * first shot of a fight spawned twenty actors in one frame, and the player is looking straight at
	 * the gun when it happens. Called from BeginPlay instead, where a hitch is invisible.
	 */
	void PrewarmProjectilePool();

protected:

public:

	UFUNCTION(BlueprintCallable, Category = "SFX")
	void PlayADSInSound();

	UFUNCTION(BlueprintCallable, Category = "SFX")
	void PlayADSOutSound();

	// ==================== Presentation ====================
	//
	// What the HUD and the inventory show for this gun. It lives here rather than on the widget
	// because the Blueprint that picks the mesh is the one that knows what the gun is called; the
	// widgets used to fall back on the object name and printed things like "BP_AR_C_0".

	/** Name on the weapon plate and in the inventory. Falls back to the class name when empty, so
	 *  a half-configured weapon is readable rather than blank. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FText WeaponDisplayName;

	/** White silhouette drawn beside the ammo count.
	 *
	 *  Editable by hand on purpose: a silhouette that already exists should just be dropped in.
	 *  GenerateIconFromMesh below fills it in from the first person mesh when there isn't one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TObjectPtr<UTexture2D> Icon;

	/** The badge beside the ammo count: rounds of this weapon's ammo with its silhouette over them,
	 *  cut to a triangle. Authored outside the engine from Icon plus the ammo art, so it is a plain
	 *  slot rather than something GenerateIconFromMesh produces.
	 *
	 *  Greyscale on purpose. It is tinted at draw time by AmmoColorTag, which is what lets the
	 *  palette recolour every badge at once instead of eight textures being rebuilt. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TObjectPtr<UTexture2D> AmmoBadge;

	/** Which entry of the palette tints this weapon's ammo.
	 *
	 *  A class's own weapon carries that class's colour; a weapon that favours a class carries a
	 *  related one. Both are just rows in Project Settings -> Polarity -> Palette, so the
	 *  relationship is authored there rather than derived here. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FGameplayTag AmmoColorTag;

	/** The shape shown, shrunk, over a pile of this weapon's ammo.
	 *
	 *  Normally the same static mesh the dropped version of this weapon wears. It cannot be read
	 *  off that actor at runtime: a Blueprint's components live in its construction script, not on
	 *  its default object, so the mesh is named here instead. One field per weapon and one ammo
	 *  pickup Blueprint for all of them, rather than a pickup Blueprint per gun.
	 *
	 *  Null just means the pile shows no weapon over it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TObjectPtr<UStaticMesh> DropPreviewMesh;

	/** What a pile of this weapon's rounds is, as an actor in the world.
	 *
	 *  Used when rounds are thrown out of the grid and when a pickup brought more than would fit.
	 *  Before this existed, leftover rounds came back as ANOTHER WHOLE WEAPON DROP, which meant a
	 *  full bag quietly printed a second copy of the gun for anybody standing nearby.
	 *
	 *  Null falls back to the inventory component's per-kind class, and if that is unset too,
	 *  throwing rounds away is refused in the log rather than deleting them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSubclassOf<class AAmmoPickup> AmmoPickupClass;

	UFUNCTION(BlueprintPure, Category = "Presentation")
	UStaticMesh* GetDropPreviewMesh() const { return DropPreviewMesh; }

	// ==================== Magazine cells ====================

	/** Every round the owner holds for this weapon, loaded ones included.
	 *
	 *  The inventory cells are the whole supply and CurrentBullets is the loaded part of it, which
	 *  is why the HUD's reserve is this minus CurrentBullets rather than this on its own.
	 *
	 *  Returns MagazineSize for a weapon that does not reload (the energy one) and for an owner
	 *  with no inventory (an NPC), so neither is dragged onto the cell economy. */
	UFUNCTION(BlueprintPure, Category = "Ammo")
	int32 GetPooledAmmo() const;

	/** Take one round out of the magazine cells. Server only, and safe to call anywhere: it does
	 *  nothing without authority, nothing for a weapon that never reloads, and nothing for an owner
	 *  with no inventory. */
	void SpendPooledRound();

	/** Charge the drop this weapon came off was carrying.
	 *
	 *  Discarding a weapon used to zero the charge on the drop, which made it scenery: pickup needs
	 *  charge, so a thrown gun could never be picked back up. It now leaves with what it arrived
	 *  with, which is what makes "drop it and take it again" work at all. */
	UPROPERTY(BlueprintReadWrite, Category = "Ammo")
	float SourceDropCharge = 0.0f;

	UFUNCTION(BlueprintPure, Category = "Presentation")
	UTexture2D* GetAmmoBadge() const { return AmmoBadge; }

	/** Palette colour for AmmoColorTag, white when the tag is unset or unnamed. White is the
	 *  identity for a tint, so an unconfigured weapon draws its badge as authored. */
	UFUNCTION(BlueprintPure, Category = "Presentation")
	FLinearColor GetAmmoColor() const;

	/** Side of the square icon texture, in pixels. */
	UPROPERTY(EditDefaultsOnly, Category = "Presentation|Icon Capture", meta = (ClampMin = "64", ClampMax = "1024"))
	int32 IconResolution = 512;

	/** Which way the gun faces in the generated icon. The capture looks at the mesh from this yaw,
	 *  so a weapon authored down a different axis is fixed here rather than in code.
	 *
	 *  0 is the side view, which is what an icon wants. The Infima weapons this project uses run
	 *  along Y, so looking down X sees their full length; 90 looks straight into the muzzle and
	 *  produces a sliver. Measured on SK_AR_02: 6.8% of the frame covered at 0, 0.75% at 90. */
	UPROPERTY(EditDefaultsOnly, Category = "Presentation|Icon Capture")
	float IconCaptureYaw = 0.0f;

	/** How big the gun is drawn in its icon. This is the size knob.
	 *
	 *  1.0 fits the mesh's bounding sphere exactly. Below 1.0 zooms IN, which is what a long thin
	 *  gun needs: its bounding sphere is nearly its whole length, so it ends up as a thin band in a
	 *  square icon while a stubby pistol fills the same square comfortably. Above 1.0 pulls back.
	 *
	 *  Per weapon on purpose. One formula cannot make a katana and a pistol look equally weighty in
	 *  the same square, so the last word is a number an author sets by eye.
	 *
	 *  Change it, then press Generate Icon From Mesh. The badge is built from the icon, so it
	 *  follows along. */
	UPROPERTY(EditDefaultsOnly, Category = "Presentation|Icon Capture", meta = (ClampMin = "0.25", ClampMax = "3.0"))
	float IconCapturePadding = 1.15f;

	/** WeaponDisplayName, or the class name when it is empty. */
	UFUNCTION(BlueprintPure, Category = "Presentation")
	FText GetWeaponDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "Presentation")
	UTexture2D* GetIcon() const { return Icon; }

#if WITH_EDITOR
	/** Render the first person mesh to a white silhouette and store it in Icon.
	 *
	 *  The silhouette is RENDERED, not filtered out of a normal shot: the mesh is drawn with an
	 *  unlit pure-white material on black, so brightness is coverage and nothing has to fight
	 *  shadows, speculars or the gun's own dark textures. The result is saved as a texture asset
	 *  next to this Blueprint, because the widget needs a UTexture2D and a loose PNG would have to
	 *  be re-imported by hand every time the mesh changes.
	 *
	 *  Safe to press again after swapping the mesh: it overwrites the same asset.
	 *
	 *  BlueprintCallable as well as CallInEditor so it can be scripted: doing all the weapons at
	 *  once is a loop, not twenty clicks. It only exists in editor builds, so do not put it in a
	 *  gameplay graph. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Presentation|Icon Capture")
	void GenerateIconFromMesh();
#endif

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

	/** This weapon's place in the hand is authored in the animation, not in a grip socket.
	 *
	 *  The LPSP animations key ik_hand_gun, and that bone IS the weapon transform: the mesh root
	 *  rides it, so the pose already says where the gun is at every frame. Landing a grip socket on
	 *  the hand on top of that replaces the animator's placement with a constant tuned by hand for
	 *  a different set of animations. Both hands then miss, because both were keyed against the gun
	 *  where the animation put it, and the off hand is the one that shows it.
	 *
	 *  On: the first person mesh hangs on AnimatedWeaponSocketName and keeps the relative transform
	 *  the attach gave it, which is identity. Off: the old path, OptionalGrip lands on the hand
	 *  socket. Third person is the same either way, the body still carries the gun on its hand.
	 *
	 *  Requires ik_hand_gun to be driven whenever such an animation is NOT playing, or the gun
	 *  drops to the reference pose the moment one ends. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	bool bWeaponPoseFromAnimation = false;

	/** Bone the first person mesh hangs on when bWeaponPoseFromAnimation is set. */
	static const FName AnimatedWeaponSocketName;

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

	/** Rounds one ammo pickup is worth to THIS weapon.
	 *
	 *  Ammo is one pool for every gun, so a pile does not belong to anything; what it is WORTH does
	 *  depend on what is in your hands, and this is where each weapon says so. Zero means "a
	 *  magazine of this weapon", which keeps a pile equal to one reload without a second number to
	 *  maintain. Read by AAmmoPickup::MakeItemFor from the weapon the taker is holding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Ammo", meta = (ClampMin = "0"))
	int32 RoundsFromPickup = 0;

	UFUNCTION(BlueprintPure, Category = "Weapon|Ammo")
	int32 GetRoundsFromPickup() const { return RoundsFromPickup > 0 ? RoundsFromPickup : FMath::Max(1, MagazineSize); }
	int32 GetBulletCount() const { return CurrentBullets; }

	/** Input action that switches/equips this weapon (per-weapon hotkey). May be null. */
	UInputAction* GetSwitchAction() const { return SwitchAction; }

	/** Hotkey slot this instance was placed in by its owner; INDEX_NONE when unplaced. @see HotkeySlot */
	UFUNCTION(BlueprintPure, Category = "Weapon|Input")
	int32 GetHotkeySlot() const { return HotkeySlot; }

	/** Server only, called by AShooterCharacter when the weapon enters or moves inside the inventory. */
	void SetHotkeySlot(int32 InSlot) { HotkeySlot = InSlot; }

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

	/**
	 * What one shot of this weapon does, whatever carries it there.
	 *
	 * The weapon owns the balance number for both paths. HitscanDamage stopped meaning "damage when
	 * tracing" the moment the projectile stopped carrying its own: it is THE number, and the name is
	 * the only thing left over from when it was not.
	 *
	 * A projectile class may still override it, and a special payload should: that is how a rocket
	 * stays a rocket after Upgrade_RocketProjectileSwap puts a different one in the tube. An ordinary
	 * bullet leaves its override negative and inherits this.
	 *
	 * Everything that has to answer "how hard does this gun hit" goes through here, which is what
	 * fixes the damage readout for projectile weapons: PredictDamageAgainst used to open with
	 * `HitscanDamage <= 0 -> return 0` and show the player a flat zero for anything firing rounds.
	 */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetShotDamage() const;

	/**
	 * The round this weapon actually puts in the air, or null when it puts nothing there.
	 *
	 * ONE place answers "is there a payload whose overrides count", and the answer is no for a
	 * weapon set to hitscan even when a ProjectileClass is still filled in -- which is an ordinary
	 * state to be in, because the shotgun fires either way and its blueprint keeps both configured.
	 * Without this gate a rocket's overrides would have leaked onto a weapon firing traces.
	 *
	 * Returns the class default object, which carries exactly the values the spawned round will
	 * have. Cheap: no spawn, no load beyond the class already referenced.
	 */
	const AShooterProjectile* GetShotPayload() const;

	/**
	 * Headshot multiplier for a shot of this weapon, after the payload has had its say.
	 *
	 * Every carrier asks this rather than reading HeadshotMultiplier, so a round that overrides it
	 * (a rocket, which should not care where on a body it went off) is honoured on the projectile
	 * path exactly as its damage override already was.
	 */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetShotHeadshotMultiplier() const;

	/**
	 * Does one shot of this weapon ionize its target, and by how much.
	 *
	 * Returns false when nothing should be charged -- either the weapon does not ionize, or the
	 * round it fires explicitly refuses to. OutChargePerHit is signed, as the weapon's own field is:
	 * a negative amount electrifies the other way, so "no ionization" cannot be expressed as zero
	 * and needs the bool.
	 */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool GetShotIonization(float& OutChargePerHit) const;

	/** Whether a shot of this weapon ionizes at all, for callers that do not need the amount.
	 *  Replaces every direct read of bUseHitscanIonization outside GetShotIonization itself: that
	 *  field is the GUN's answer, and the round it fires may have a different one. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool DoesShotIonize() const;

	/**
	 * Every multiplier this weapon can work out about a target at the instant of the shot, folded
	 * into one number: heat, height advantage, target tags, the owner's upgrades.
	 *
	 * Assembled in one place so the trace path, the bolt and the projectile cannot drift apart on
	 * what a hit is worth. Deliberately NOT including the headshot multiplier or the shield gate:
	 * those need the hit itself and are applied inside ApplyWeaponHit.
	 */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetShotDamageMultiplierAgainst(AActor* Target) const;

	/**
	 * Which bone a shot that landed on this actor actually struck.
	 *
	 * Needed because NOTHING that resolves a hit in this game gets the bone for free, and the reason
	 * is the same everywhere: a character answers the Pawn object channel with TWO shapes, the
	 * capsule and the skeletal mesh, and the capsule is the one that encloses the other. A sweep
	 * returns it first, a projectile's sphere blocks against it, and a capsule hit carries no bone
	 * at all. So the head was being shot and the shot was being read off the wrong shape.
	 *
	 * This traces the mesh COMPONENT directly (LineTraceComponent), which is what makes it work:
	 * the CharacterMesh profile ignores Visibility, so an ordinary channel trace cannot see the
	 * body at all. NAME_None means the line missed the mesh, which is a legitimate answer -- the
	 * capsule is wider than the body, and a shot can clip it while passing beside the ribs.
	 *
	 * One trace, run once per shot on the target that was already chosen, so it costs nothing until
	 * something has been hit.
	 */
	FName ResolveHitBone(const AActor* Target, const FVector& Start, const FVector& End) const;

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

	/** How many times the sights magnify, with the mounted optic folded in. See ADSZoom.
	 *
	 *  Out of line because the optic is a lookup: this is the single funnel the whole game uses to
	 *  turn magnification into a field of view, so multiplying here is what makes an attachment
	 *  change the aim everywhere at once instead of at each call site. */
	UFUNCTION(BlueprintPure, Category = "Weapon|ADS")
	float GetADSZoom() const;

	// ==================== Attachments ====================
	//
	// The WEAPON is the truth about what is mounted, not the inventory. Three reasons, and each one
	// on its own is enough: an attachment travels with the gun when the gun changes hands, the gun
	// already replicates so teammates see the scope for free, and a weapon on the floor or in an
	// NPC's hands has no inventory to ask.
	//
	// What the inventory keeps is only the CELL an attachment costs once the free slots are used
	// up. That cell holds no copy of the attachment: it points at the same asset and at the weapon
	// it is mounted on, so the two records cannot disagree about what is fitted.

	/** Mount an attachment. Server only; the array replicates and every machine rebuilds its meshes
	 *  from OnRep.
	 *
	 *  Refuses when this weapon already carries that type: an attachment is replaced by taking the
	 *  old one off first, so the cell it was paying for is settled before the new one arrives. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Attachments")
	bool InstallAttachment(UWeaponAttachmentDefinition* Attachment);

	/** Take the attachment of this type off. Returns what came off, or null when nothing was
	 *  mounted. Server only. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Attachments")
	UWeaponAttachmentDefinition* UninstallAttachmentOfType(EWeaponAttachmentType InType);

	UFUNCTION(BlueprintPure, Category = "Weapon|Attachments")
	UWeaponAttachmentDefinition* GetAttachmentOfType(EWeaponAttachmentType InType) const;

	/** Everything mounted, in the order it was mounted. That order is what decides which ones are
	 *  free and which ones cost a cell, so it is deliberately not sorted. */
	const TArray<TObjectPtr<UWeaponAttachmentDefinition>>& GetInstalledAttachments() const { return InstalledAttachments; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Attachments")
	int32 GetInstalledAttachmentCount() const { return InstalledAttachments.Num(); }

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
