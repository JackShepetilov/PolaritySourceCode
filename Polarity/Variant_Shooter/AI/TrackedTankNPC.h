// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterNPC.h"
#include "TrackedTankNPC.generated.h"

class UStaticMeshComponent;
class UNiagaraSystem;
class UNiagaraComponent;

/**
 * Visible damage progression on the tank hull (same shape as the flying drone's stages):
 * thresholds are fractions of spawn HP, the stage drives speed penalties and hull VFX.
 */
UENUM(BlueprintType)
enum class ETankDamageStage : uint8
{
	Intact UMETA(DisplayName = "Intact"),
	Damaged UMETA(DisplayName = "Damaged"),
	Critical UMETA(DisplayName = "Critical")
};

/**
 * Tracked unmanned tank - the heavy/specialist slot of the robot faction (faction B design in
 * Faction_War_Master_Plan_2026-08-25.md §3; behaviour decisions in
 * Docs/TrackedTank_Design_2026-08-25.md).
 *
 * Role: siege breaker on the attack, sector anchor on defence. Two barrels: the main gun (a slow,
 * loud projectile weapon with a long visible reload - THE counter-play rhythm) and a machine gun
 * that keeps firing while the main gun reloads. The tank is never helpless.
 *
 * The machine gun IS the base-class weapon: set WeaponClass (or the combat profile override) to
 * the machine-gun blueprint. The main gun spawns separately from CannonWeaponClass.
 *
 * Survivability: standard project scheme - EMF charge shield over HP. Damage stages are visual
 * AND functional; hits landing low on the hull accumulate track damage until the tank is
 * immobilized (immobile but its guns stay dangerous - MGSV pattern).
 *
 * Movement: plain NavMesh walking (CMC), slow. Death: charge-scaled explosion with honest
 * attribution (ResolveExplosionInstigator pattern), no ragdoll/GC wreck in v1.
 */
UCLASS()
class POLARITY_API ATrackedTankNPC : public AShooterNPC
{
	GENERATED_BODY()

public:

	ATrackedTankNPC(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// ==================== Weapons ====================

	/** Main gun: slow projectile weapon, magazine of one, long reload (configured on the weapon
	 *  blueprint - no special code). Null = tank fights with the machine gun only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Weapons")
	TSubclassOf<AShooterWeapon> CannonWeaponClass;

	/** Switch fire to the main gun */
	UFUNCTION(BlueprintCallable, Category = "Tank|Weapons")
	void SelectMainGun();

	/** Switch fire to the machine gun */
	UFUNCTION(BlueprintCallable, Category = "Tank|Weapons")
	void SelectMachineGun();

	UFUNCTION(BlueprintPure, Category = "Tank|Weapons")
	bool IsMainGunSelected() const { return bMainGunActive; }

protected:

	/** The machine gun - the weapon the base class spawned from WeaponClass */
	TObjectPtr<AShooterWeapon> MachineGun;

	/** The main gun - spawned by this class from CannonWeaponClass */
	TObjectPtr<AShooterWeapon> CannonWeapon;

	bool bMainGunActive = false;

	/** Point `Weapon` at the requested barrel, stopping any burst in progress */
	void SelectBarrel(bool bMainGun);

	/** Show/hide third-person meshes so the idle barrel reads visually */
	void UpdateBarrelVisibility();

	// ==================== Body ====================

	/** Anchor for hull VFX. Kept as a transform after the hull became a skeletal mesh: the stage
	 *  FX components hang off it, and moving them would change where sparks and fire appear. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TankMesh;

	virtual UMeshComponent* GetHitFlashMeshComponent() const override;

	/** Attach a barrel's meshes to the hull; the cannon sits front-low, the MG top-rear */
	virtual void AttachWeaponMeshes(AShooterWeapon* WeaponToAttach) override;

	/** Third-person mesh offset for the main gun on the hull */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Visual")
	FVector CannonMeshOffset = FVector(140.0f, 0.0f, 20.0f);

	/** Third-person mesh offset for the machine gun on the hull */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Visual")
	FVector MGMeshOffset = FVector(-40.0f, 0.0f, 80.0f);

	// ==================== Turret ====================

public:

	/** Point the turret at a world location. The turret swings at a limited rate, so this is a
	 *  request, not a snap: the swing IS the telegraph the player reads before a shell arrives. */
	UFUNCTION(BlueprintCallable, Category = "Tank|Turret")
	void SetTurretAimLocation(const FVector& WorldLocation);

	/** Stop tracking; the turret drifts back to straight ahead */
	UFUNCTION(BlueprintCallable, Category = "Tank|Turret")
	void ClearTurretAim();

	/** True once the turret has actually caught up with what it was told to look at. Fire gates on
	 *  this: a shell that leaves while the barrel is still swinging goes nowhere near the aim. */
	UFUNCTION(BlueprintPure, Category = "Tank|Turret")
	bool IsTurretOnTarget(float ToleranceDegrees = 3.0f) const;

	UFUNCTION(BlueprintPure, Category = "Tank|Turret")
	float GetTurretYaw() const { return TurretYaw; }

	UFUNCTION(BlueprintPure, Category = "Tank|Turret")
	float GetGunPitch() const { return GunPitch; }

protected:

	/** Turret angle in hull space. Replicated because the decision is made by an AI controller,
	 *  which does not exist on clients: without this an observer sees a barrel pointing the wrong
	 *  way while shells come out of it. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Tank|Turret")
	float TurretYaw = 0.0f;

	/** Gun elevation in hull space, positive is up */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Tank|Turret")
	float GunPitch = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Turret", meta = (ClampMin = "5.0", ClampMax = "360.0"))
	float TurretYawRate = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Turret", meta = (ClampMin = "5.0", ClampMax = "360.0"))
	float GunPitchRate = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Turret", meta = (ClampMin = "-45.0", ClampMax = "0.0"))
	float GunPitchMin = -8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Turret", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float GunPitchMax = 25.0f;

	/** Height of the turret ring above the hull origin, used as the origin of the aim vector */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Turret")
	float TurretPivotHeight = 105.0f;

	/** Server-side: what the turret was last asked to look at */
	FVector TurretAimLocation = FVector::ZeroVector;
	bool bHasTurretAim = false;

	/** Server-side: where the turret is trying to get to, kept for IsTurretOnTarget */
	float TurretDesiredYaw = 0.0f;
	float TurretDesiredPitch = 0.0f;

	/** Server-side: the turret has something to track this frame, explicit request or live enemy */
	bool bTurretTracking = false;

	/** Move the turret toward its request at the configured rates (authority only) */
	void UpdateTurret(float DeltaSeconds);

	// ==================== Threat ====================

	/** How much attention this hull draws once factions target each other. Analog of the player
	 *  Tank class's threat weight - the biggest among faction B. Consumed by faction targeting
	 *  (intent channel), which does not exist yet. TODO(factions): wire into targeting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Combat")
	float BaseThreat = 100.0f;

	// ==================== Damage Stages ====================

	/** HP fraction at or below which the tank counts as Damaged (sparks + smoke, slower). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Damage Stages", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float DamagedStageHPFraction = 0.6f;

	/** HP fraction at or below which the tank counts as Critical (burning, slowest). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Damage Stages", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CriticalStageHPFraction = 0.3f;

	/** Movement speed multiplier while Damaged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Damage Stages", meta = (ClampMin = "0.2", ClampMax = "1.0"))
	float DamagedSpeedMultiplier = 0.85f;

	/** Movement speed multiplier while Critical. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Damage Stages", meta = (ClampMin = "0.2", ClampMax = "1.0"))
	float CriticalSpeedMultiplier = 0.7f;

	/** Current visible damage stage (replicated so clients activate matching hull VFX). */
	UPROPERTY(ReplicatedUsing = OnRep_TankDamageStage, BlueprintReadOnly, Category = "Tank|Damage Stages")
	ETankDamageStage TankDamageStage = ETankDamageStage::Intact;

	/** Sparks + smoke system while Damaged (attached to TankMesh). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|VFX")
	TObjectPtr<UNiagaraSystem> DamagedHullFX;

	/** Burning system while Critical (attached to TankMesh). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|VFX")
	TObjectPtr<UNiagaraSystem> CriticalHullFX;

	// ==================== Tracks ====================

	/** Hits whose impact lands below this fraction of the capsule height count as track hits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Tracks", meta = (ClampMin = "0.05", ClampMax = "0.9"))
	float TrackHitHeightFraction = 0.35f;

	/** Accumulated track-zone damage that breaks the running gear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Tracks", meta = (ClampMin = "1.0"))
	float TrackDamageToImmobilize = 200.0f;

	/** True when the running gear is broken: immobile, guns still dangerous (replicated for
	 *  client-side sparks/VFX later). No self-repair yet - see design doc open question. */
	UPROPERTY(ReplicatedUsing = OnRep_bImmobilized, BlueprintReadOnly, Category = "Tank|Tracks")
	bool bImmobilized = false;

public:

	UFUNCTION(BlueprintPure, Category = "Tank|Tracks")
	bool IsImmobilized() const { return bImmobilized; }

protected:

	float TrackAccumulatedDamage = 0.0f;

	/** Classify a damage event against the track zone (call BEFORE Super consumes the event) */
	void RegisterTrackHit(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser);

	/** Break the running gear */
	void Immobilize();

	UFUNCTION()
	void OnRep_bImmobilized();

	// ==================== Lifecycle & Overrides ====================

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void Die() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void ResetForPool(const FVector& NewLocation, const FRotator& NewRotation) override;

	// ==================== Damage Stage Internals ====================

	/** Recompute the stage from current HP and apply speed/VFX consequences. Server-side; clients
	 *  mirror the VFX in OnRep_TankDamageStage. */
	void UpdateTankStage();

	/** Movement speed multiplier for the current stage (1.0 while Intact). */
	float GetStageSpeedMultiplier() const;

	/** Activate/deactivate hull FX components for the given stage (safe on server and client). */
	void ApplyStageVFX(ETankDamageStage Stage);

	UFUNCTION()
	void OnRep_TankDamageStage();

	// ==================== Death Explosion ====================

	/** If true, tank explodes on death instead of the parent's death visuals */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Death")
	bool bExplodeOnDeath = true;

	/** Explosion damage radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Death", meta = (EditCondition = "bExplodeOnDeath"))
	float ExplosionRadius = 350.0f;

	/** Explosion damage amount */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Death", meta = (EditCondition = "bExplodeOnDeath"))
	float ExplosionDamage = 60.0f;

	/** If true, explosion stuns nearby NPCs (same as prop/drone explosions) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Death", meta = (EditCondition = "bExplodeOnDeath"))
	bool bApplyExplosionStun = true;

	/** Duration of the explosion stun (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Death", meta = (EditCondition = "bExplodeOnDeath", ClampMin = "0.1"))
	float ExplosionStunDuration = 2.5f;

	/** Scale explosion damage/radius/VFX by charge magnitude (like drones/props) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Death", meta = (EditCondition = "bExplodeOnDeath"))
	bool bScaleExplosionWithCharge = true;

	/** Reference charge for scaling: |charge| / this = scale factor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Death", meta = (EditCondition = "bExplodeOnDeath && bScaleExplosionWithCharge", ClampMin = "1.0"))
	float ExplosionReferenceCharge = 50.0f;

	/** Minimum charge scale (clamp low end) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Death", meta = (EditCondition = "bExplodeOnDeath && bScaleExplosionWithCharge", ClampMin = "0.1", ClampMax = "1.0"))
	float MinChargeScale = 0.5f;

	/** Maximum charge scale (clamp high end) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Death", meta = (EditCondition = "bExplodeOnDeath && bScaleExplosionWithCharge", ClampMin = "1.0", ClampMax = "5.0"))
	float MaxChargeScale = 2.5f;

	/** Niagara system for the death explosion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|VFX")
	TObjectPtr<UNiagaraSystem> ExplosionFX;

	/** Scale of the explosion effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|VFX")
	float ExplosionFXScale = 1.5f;

	/** Sound to play on explosion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|SFX")
	TObjectPtr<USoundBase> ExplosionSound;

	/** Charge-scaled radial explosion with honest attribution (called before Super::Die so the
	 *  EMF charge is still readable) */
	void TriggerExplosion();

private:

	/** Runtime FX components (created in constructor, attached to TankMesh) */
	UPROPERTY(VisibleAnywhere, Category = "Tank|VFX")
	TObjectPtr<UNiagaraComponent> DamagedHullFXComponent;

	UPROPERTY(VisibleAnywhere, Category = "Tank|VFX")
	TObjectPtr<UNiagaraComponent> CriticalHullFXComponent;

	/** Spawn HP reference for stage fractions */
	float MaxHPAtSpawn = 0.0f;

	/** Configured walk speed before stage multipliers */
	float BaseMaxWalkSpeed = 0.0f;
};
