// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterNPC.h"
#include "FlyingDrone.generated.h"

class UFlyingAIMovementComponent;
class USphereComponent;
class UNiagaraSystem;
class UNiagaraComponent;

/** Visible damage progression on the drone hull. Thresholds are fractions of the HP the drone
 *  spawned with; the stage drives speed penalties, overheat frequency and hull VFX. */
UENUM(BlueprintType)
enum class EDroneDamageStage : uint8
{
	Intact UMETA(DisplayName = "Intact"),
	Damaged UMETA(DisplayName = "Damaged"),
	Critical UMETA(DisplayName = "Critical")
};

/**
 * Flying drone enemy - a hovering robot soldier from an alien civilization.
 * Inherits weapon handling and damage systems from ShooterNPC.
 * Uses UFlyingAIMovementComponent for 3D navigation.
 *
 * Knockback uses the same interpolation system as ShooterNPC (SetActorLocation-based),
 * which provides reliable wall slam damage detection and wall bounce.
 * Gravity is automatically skipped because GravityScale = 0.
 */
UCLASS()
class POLARITY_API AFlyingDrone : public AShooterNPC
{
	GENERATED_BODY()

public:

	AFlyingDrone(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	// ==================== Components ====================

	/** Flying AI movement component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UFlyingAIMovementComponent> FlyingMovement;

	/** Sphere collision for the drone body */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> DroneCollision;

	/** Visual mesh for the drone (placeholder sphere, replace with actual mesh later) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> DroneMesh;

	// ==================== Drone Settings ====================

	/** Radius of the drone's collision sphere (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Collision")
	float CollisionRadius = 50.0f;

	/** If true, drone explodes on death instead of falling */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Death")
	bool bExplodeOnDeath = true;

	/** Explosion damage radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Death", meta = (EditCondition = "bExplodeOnDeath"))
	float ExplosionRadius = 200.0f;

	/** Explosion damage amount */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Death", meta = (EditCondition = "bExplodeOnDeath"))
	float ExplosionDamage = 30.0f;

	/** If true, explosion stuns nearby NPCs (same as prop explosion) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Death", meta = (EditCondition = "bExplodeOnDeath"))
	bool bApplyExplosionStun = true;

	/** Duration of the explosion stun (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Death", meta = (EditCondition = "bExplodeOnDeath", ClampMin = "0.1"))
	float ExplosionStunDuration = 2.0f;

	/** Anim montage to play on stunned NPCs (null = fallback to NPC's KnockbackMontage) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Death", meta = (EditCondition = "bExplodeOnDeath"))
	TObjectPtr<UAnimMontage> ExplosionStunMontage;

	/** Time before destruction after death (for effects to play) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Death")
	float DeathEffectDuration = 1.0f;

	/** Scale explosion damage/radius/VFX by charge magnitude (like EMFPhysicsProp) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Death", meta = (EditCondition = "bExplodeOnDeath"))
	bool bScaleExplosionWithCharge = true;

	/** Reference charge for scaling: |charge| / this = scale factor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Death", meta = (EditCondition = "bExplodeOnDeath && bScaleExplosionWithCharge", ClampMin = "1.0"))
	float ExplosionReferenceCharge = 50.0f;

	/** Minimum charge scale (clamp low end) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Death", meta = (EditCondition = "bExplodeOnDeath && bScaleExplosionWithCharge", ClampMin = "0.1", ClampMax = "1.0"))
	float MinChargeScale = 0.5f;

	/** Maximum charge scale (clamp high end) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Death", meta = (EditCondition = "bExplodeOnDeath && bScaleExplosionWithCharge", ClampMin = "1.0", ClampMax = "5.0"))
	float MaxChargeScale = 2.0f;

	/** Angular spin speed (deg/s) applied to drone mesh when launched. 0 = no spin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Combat", meta = (ClampMin = "0.0", ClampMax = "3600.0"))
	float LaunchSpinSpeed = 720.0f;

	// ==================== Combat Settings ====================

	/** If true, drone will automatically shoot at visible enemies (legacy - disable for StateTree control) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Combat")
	bool bAutoEngage = false;

	/** Distance at which drone can see and engage targets */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Combat")
	float EngageRange = 2000.0f;

	/** How often to check for targets (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Combat")
	float TargetCheckInterval = 0.25f;

	// ==================== Evasive Dash Settings (for StateTree) ====================

	/** Cooldown for evasive dash after taking damage (seconds). Long by design: the dash used to
	 *  fire every few seconds and wreck target tracking; between dashes evasion is a FirePosition
	 *  relocation, not a local jerk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Combat|Evasion", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float EvasiveDashCooldown = 10.0f;

	/** Time of last evasive dash (for cooldown tracking) */
	float LastEvasiveDashTime = -100.0f;

	/** If true, drone took damage this frame (reset each tick, for StateTree condition) */
	bool bTookDamageThisFrame = false;

	/** Time when last damage was taken (for StateTree condition with grace period) */
	float LastDamageTakenTime = -100.0f;

	// ==================== Damage Stages ====================

	/** HP fraction at or below which the drone counts as Damaged (sparks + smoke, speed penalty). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Damage Stages", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float DamagedStageHPFraction = 0.6f;

	/** HP fraction at or below which the drone counts as Critical (burning, bigger speed penalty,
	 *  overheats more often). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Damage Stages", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CriticalStageHPFraction = 0.3f;

	/** Movement speed multiplier while Damaged (applied to FlyingMovement fly speed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Damage Stages", meta = (ClampMin = "0.2", ClampMax = "1.0"))
	float DamagedSpeedMultiplier = 0.85f;

	/** Movement speed multiplier while Critical. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Damage Stages", meta = (ClampMin = "0.2", ClampMax = "1.0"))
	float CriticalSpeedMultiplier = 0.75f;

	/** Overheat duration multiplier while Critical (< 1 = overheats more often, shorter window
	 *  between bursts means the drone shoots more often but from a worse position). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Damage Stages", meta = (ClampMin = "0.2", ClampMax = "1.0"))
	float CriticalOverheatScale = 0.7f;

	/** Current visible damage stage (replicated so clients activate the matching hull VFX). */
	UPROPERTY(ReplicatedUsing = OnRep_DroneDamageStage, BlueprintReadOnly, Category = "Drone|Damage Stages")
	EDroneDamageStage DamageStage = EDroneDamageStage::Intact;

	/** Sparks + smoke system active while Damaged (attached to DroneMesh). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|VFX")
	TObjectPtr<UNiagaraSystem> DamagedHullFX;

	/** Burning + arc discharge system active while Critical (attached to DroneMesh). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|VFX")
	TObjectPtr<UNiagaraSystem> CriticalHullFX;

	/** Runtime hull FX components (created in constructor, attached to DroneMesh). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone|VFX")
	TObjectPtr<UNiagaraComponent> DamagedHullFXComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone|VFX")
	TObjectPtr<UNiagaraComponent> CriticalHullFXComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone|VFX")
	TObjectPtr<UNiagaraComponent> RepairHealFXComponent;

	// ==================== Repair Retreat ====================

	/** Cooldown between repair retreats (seconds). Not once-per-life: a big cooldown instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Repair", meta = (ClampMin = "5.0"))
	float RepairCooldown = 75.0f;

	/** Accumulated damage since the last repair that triggers a retreat on its own (even above
	 *  the Critical threshold stage). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Repair", meta = (ClampMin = "1.0"))
	float RepairDamageThreshold = 120.0f;

	/** Height above the retreat anchor the drone climbs to (cm). ~100 m: nearly invisible from
	 *  below but still hittable — spotting and finishing a repairing drone is the reward for
	 *  paying attention. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Repair", meta = (ClampMin = "1000.0"))
	float RepairAltitude = 10000.0f;

	/** HP restored per second while hovering at the repair altitude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Repair", meta = (ClampMin = "1.0"))
	float RepairHPPerSecond = 25.0f;

	/** A single hit of at least this damage aborts the repair immediately; the drone returns to
	 *  combat without a second ascent until the cooldown elapses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Repair", meta = (ClampMin = "1.0"))
	float RepairInterruptDamage = 20.0f;

	/** Weak heal VFX while repairing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|VFX")
	TObjectPtr<UNiagaraSystem> RepairHealFX;

	// Runtime repair state
	bool bIsRepairing = false;
	bool bRepairHovering = false;
	float LastRepairEndTime = -1000.0f;
	float DamageSinceLastRepair = 0.0f;
	FVector RepairAnchorLocation = FVector::ZeroVector;
	float MaxHPAtSpawn = 0.0f;

	/** FlyingMovement fly speed as configured (before damage-stage multipliers). */
	float BaseFlySpeed = 0.0f;

	// ==================== Stabilization Settings ====================

	/** Spring constant - how aggressively drone returns to level (higher = snappier) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Stabilization", meta = (ClampMin = "1.0", ClampMax = "50.0"))
	float StabilizationSpring = 15.0f;

	/** Damping coefficient - how quickly oscillation dies out (higher = less wobble) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Stabilization", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float StabilizationDamping = 8.0f;

	/** Angular impulse per point of damage (degrees/sec per damage point) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Stabilization", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float AngularImpulsePerDamage = 8.0f;

	/** Maximum angular velocity cap (degrees/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Stabilization", meta = (ClampMin = "30.0", ClampMax = "1080.0"))
	float MaxAngularVelocity = 360.0f;

	/** Maximum tilt angle from level (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Stabilization", meta = (ClampMin = "5.0", ClampMax = "90.0"))
	float MaxTiltAngle = 45.0f;

	/** Random yaw spin added on each hit (degrees/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Stabilization", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float ImpulseYawRandomness = 30.0f;

	/** Mesh rotation offset when captured by channeling (belly toward player).
	 *  Tune axes if drone mesh has non-standard orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Stabilization")
	FRotator CapturedTiltOffset = FRotator(-70.0f, 0.0f, 0.0f);

	/** Current angular velocity of the mesh tilt (degrees/sec, local space: X=Roll, Y=Pitch, Z=Yaw) */
	FVector MeshAngularVelocity = FVector::ZeroVector;

	// ==================== Visual Settings ====================

	/** Color of the drone's emissive elements (for material parameter) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Visual")
	FLinearColor DroneEmissiveColor = FLinearColor(1.0f, 0.2f, 0.1f, 1.0f);

	/** Intensity of emissive glow */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Visual")
	float EmissiveIntensity = 5.0f;

	// ==================== VFX ====================

	/** Niagara system for explosion on death */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|VFX")
	TObjectPtr<UNiagaraSystem> ExplosionFX;

	/** Scale of the explosion effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|VFX")
	float ExplosionFXScale = 1.0f;

	/** Niagara system for muzzle flash when shooting */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|VFX")
	TObjectPtr<UNiagaraSystem> MuzzleFlashFX;

	/** Scale of the muzzle flash effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|VFX")
	float MuzzleFlashScale = 0.5f;

	/** Offset from drone center for muzzle flash */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|VFX")
	FVector MuzzleFlashOffset = FVector(60.0f, 0.0f, -20.0f);

	/** Niagara system for thruster/hover effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|VFX")
	TObjectPtr<UNiagaraSystem> ThrusterFX;

	// ==================== SFX ====================

	/** Sound to play on explosion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|SFX")
	TObjectPtr<USoundBase> ExplosionSound;

	/** Sound to play when shooting */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|SFX")
	TObjectPtr<USoundBase> ShootSound;

protected:

	// ==================== Lifecycle ====================

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// ==================== Damage Stages ====================

public:

	/** Movement speed multiplier for the current stage (1.0 while Intact). */
	UFUNCTION(BlueprintPure, Category = "Drone|Damage Stages")
	float GetStageSpeedMultiplier() const;

	/** Overheat duration multiplier for the current stage (1.0 while Intact). */
	UFUNCTION(BlueprintPure, Category = "Drone|Damage Stages")
	float GetStageOverheatScale() const;

	UFUNCTION(BlueprintPure, Category = "Drone|Damage Stages")
	EDroneDamageStage GetDamageStage() const { return DamageStage; }

	// ==================== Repair Retreat ====================

	/** True when the drone should break off and repair: entered Critical, or accumulated enough
	 *  damage since the last repair, with the cooldown elapsed. */
	UFUNCTION(BlueprintPure, Category = "Drone|Repair")
	bool ShouldBeginRepair() const;

	/** Break off combat and climb vertically to the repair altitude. Returns false if a retreat
	 *  is impossible right now (already repairing, dead). */
	UFUNCTION(BlueprintCallable, Category = "Drone|Repair")
	bool BeginRepairRetreat();

	/** Finish the retreat: bCompleted=true after a full heal, false when interrupted. The drone
	 *  stays where it is; the StateTree picks the next FirePosition from here. */
	UFUNCTION(BlueprintCallable, Category = "Drone|Repair")
	void EndRepairRetreat(bool bCompleted);

	UFUNCTION(BlueprintPure, Category = "Drone|Repair")
	bool IsRepairing() const { return bIsRepairing; }

protected:

	// ==================== Overrides from ShooterNPC ====================

	/** Override damage handling to trigger drone-specific death */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Override hit flash mesh — drone uses DroneMesh (StaticMesh) instead of GetMesh() (SkeletalMesh) */
	virtual UMeshComponent* GetHitFlashMeshComponent() const override;

	/** Override to apply spin on launch */
	virtual void EnterLaunchedState() override;

	/** Override weapon attachment for drone body */
	virtual void AttachWeaponMeshes(AShooterWeapon* WeaponToAttach) override;

	/** Override aim calculation - drones aim from their center */
	virtual FVector GetWeaponTargetLocation() override;

	/** Override knockback to add drone-specific setup (stop FlyingMovement, ignore player collision)
	 *  then delegate to Super::ApplyKnockback for interpolation-based movement */
	virtual void ApplyKnockback(const FVector& KnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation = FVector::ZeroVector, bool bKeepEMFEnabled = false, EKnockbackStyle Style = EKnockbackStyle::Standard) override;

	/** Override to restore flying mode after knockback */
	virtual void EndKnockbackStun() override;

	/** Override to prevent parent's OnCapsuleHit from running during knockback
	 *  (parent's interpolation system handles wall collisions via CheckKnockbackWallCollision) */
	virtual void OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit) override;

	/** Override to stop FlyingMovement and reset angular velocity on capture */
	virtual void EnterCapturedState(UAnimMontage* OverrideMontage = nullptr) override;

	/** Override to restore flying mode after capture release */
	virtual void ExitCapturedState() override;

public:

	// ==================== Flying Movement Interface ====================

	/** Get the flying movement component */
	UFUNCTION(BlueprintPure, Category = "Drone|Movement")
	UFlyingAIMovementComponent* GetFlyingMovement() const { return FlyingMovement; }

	/** Command drone to fly to location */
	UFUNCTION(BlueprintCallable, Category = "Drone|Movement")
	void FlyTo(const FVector& Location);

	/** Command drone to fly to actor */
	UFUNCTION(BlueprintCallable, Category = "Drone|Movement")
	void FlyToTarget(AActor* Target);

	/** Command drone to perform evasive maneuver */
	UFUNCTION(BlueprintCallable, Category = "Drone|Movement")
	bool PerformEvasion(const FVector& ThreatLocation);

	/** Command drone to patrol (fly to random point) */
	UFUNCTION(BlueprintCallable, Category = "Drone|Movement")
	void StartPatrol();

	/** Stop patrolling */
	UFUNCTION(BlueprintCallable, Category = "Drone|Movement")
	void StopPatrol();

	/** Stop all movement */
	UFUNCTION(BlueprintCallable, Category = "Drone|Movement")
	void StopMovement();

	// ==================== State Queries ====================

	/** Returns true if drone is currently moving */
	UFUNCTION(BlueprintPure, Category = "Drone|State")
	bool IsFlying() const;

	/** Returns true if drone is dashing */
	UFUNCTION(BlueprintPure, Category = "Drone|State")
	bool IsDashing() const;

	/** Returns true if drone is in patrol mode */
	UFUNCTION(BlueprintPure, Category = "Drone|State")
	bool IsPatrolling() const { return bIsPatrolling; }

	// ==================== Combat Interface ====================

	/** Start shooting at target */
	UFUNCTION(BlueprintCallable, Category = "Drone|Combat")
	void EngageTarget(AActor* Target);

	/** Stop shooting */
	UFUNCTION(BlueprintCallable, Category = "Drone|Combat")
	void DisengageTarget();

	/** Check if we have line of sight to target (override from ShooterNPC) */
	virtual bool HasLineOfSightTo(AActor* Target) const override;

	/** Get current combat target */
	UFUNCTION(BlueprintPure, Category = "Drone|Combat")
	AActor* GetCombatTarget() const { return CurrentAimTarget.Get(); }

	// ==================== StateTree Support ====================

	/** Check if evasive dash is off cooldown */
	UFUNCTION(BlueprintPure, Category = "Drone|Combat|Evasion")
	bool CanPerformEvasiveDash() const;

	/** Perform evasive dash in random direction and start cooldown */
	UFUNCTION(BlueprintCallable, Category = "Drone|Combat|Evasion")
	bool PerformRandomEvasiveDash();

	/** Check if drone took damage recently (within grace period) */
	UFUNCTION(BlueprintPure, Category = "Drone|Combat|Evasion")
	bool TookDamageRecently(float GracePeriod = 0.5f) const;

	/** Reset the damage taken flag (called by StateTree after handling) */
	UFUNCTION(BlueprintCallable, Category = "Drone|Combat|Evasion")
	void ClearDamageTakenFlag() { bTookDamageThisFrame = false; }

protected:

	// ==================== Death Handling ====================

	/** Handle drone-specific death behavior */
	void DroneDie();

	/** Override: use DroneMesh transform instead of SkeletalMesh */
	virtual void SpawnDeathGeometryCollection(const FDeathModeConfig& Config) override;

	/** Called to trigger explosion effect and damage */
	void TriggerExplosion();

	/** Called when drone should fall after death (if not exploding) */
	void StartDeathFall();

	/** Deferred destruction after death effects */
	void DeathDestroy();

	// ==================== Movement Callbacks ====================

	/** Called when movement to target completes */
	UFUNCTION()
	void OnMovementCompleted(bool bSuccess);

	// ==================== Visual Updates ====================

	/** Update drone visual state (rotation towards movement, etc.) */
	void UpdateDroneVisuals(float DeltaTime);

	/** Rotate drone to face movement direction or target */
	void UpdateDroneRotation(float DeltaTime);

	/** PD controller: apply restoring torque and damping, integrate angular velocity, update mesh rotation */
	void UpdateStabilization(float DeltaTime);

	/** Apply angular impulse from a hit (called from TakeDamage) */
	void ApplyAngularImpulse(const FVector& HitDirection, float Damage);

	// ==================== VFX Methods ====================

	/** Spawn explosion effect at drone location */
	void SpawnExplosionEffect();

	/** Spawn muzzle flash effect */
	void SpawnMuzzleFlashEffect();

	// ==================== Damage Stage Internals ====================

	/** Recompute the damage stage from current HP and apply speed/VFX consequences. Server-side;
	 *  clients mirror the VFX in OnRep_DroneDamageStage. */
	void UpdateDamageStage();

	/** Activate/deactivate hull FX components for the given stage (safe on server and client). */
	void ApplyStageVFX(EDroneDamageStage Stage);

	UFUNCTION()
	void OnRep_DroneDamageStage();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Per-tick repair update: climb, then hover and heal until full HP or interrupted. */
	void TickRepair(float DeltaTime);

	// ==================== Combat Logic ====================

	/** Check for enemies and engage if found */
	void UpdateCombat();

	/** Find closest visible enemy */
	AActor* FindClosestEnemy() const;

	/** Timer handle for combat updates */
	FTimerHandle CombatTimerHandle;

private:

	/** Timer for death sequence */
	FTimerHandle DeathSequenceTimer;

	/** If true, death sequence has started */
	bool bDeathSequenceStarted = false;

	/** If true, drone is in continuous patrol mode */
	bool bIsPatrolling = false;

	/** Actor to ignore collision with during knockback (typically the player who hit us) */
	TWeakObjectPtr<AActor> KnockbackIgnoreActor;
};
