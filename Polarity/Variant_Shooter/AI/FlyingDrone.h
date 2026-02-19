// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterNPC.h"
#include "FlyingDrone.generated.h"

class UFlyingAIMovementComponent;
class USphereComponent;
class UNiagaraSystem;
class UNiagaraComponent;

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

	/** Time before destruction after death (for effects to play) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Death")
	float DeathEffectDuration = 1.0f;

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

	/** Tag to identify enemies (usually "Player") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Combat")
	FName EnemyTag = FName("Player");

	// ==================== Evasive Dash Settings (for StateTree) ====================

	/** Cooldown for evasive dash after taking damage (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Combat|Evasion", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float EvasiveDashCooldown = 3.0f;

	/** Time of last evasive dash (for cooldown tracking) */
	float LastEvasiveDashTime = -100.0f;

	/** If true, drone took damage this frame (reset each tick, for StateTree condition) */
	bool bTookDamageThisFrame = false;

	/** Time when last damage was taken (for StateTree condition with grace period) */
	float LastDamageTakenTime = -100.0f;

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

	// ==================== Overrides from ShooterNPC ====================

	/** Override damage handling to trigger drone-specific death */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Override weapon attachment for drone body */
	virtual void AttachWeaponMeshes(AShooterWeapon* WeaponToAttach) override;

	/** Override aim calculation - drones aim from their center */
	virtual FVector GetWeaponTargetLocation() override;

	/** Override knockback to add drone-specific setup (stop FlyingMovement, ignore player collision)
	 *  then delegate to Super::ApplyKnockback for interpolation-based movement */
	virtual void ApplyKnockback(const FVector& KnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation = FVector::ZeroVector, bool bKeepEMFEnabled = false) override;

	/** Override to restore flying mode after knockback */
	virtual void EndKnockbackStun() override;

	/** Override to prevent parent's OnCapsuleHit from running during knockback
	 *  (parent's interpolation system handles wall collisions via CheckKnockbackWallCollision) */
	virtual void OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit) override;

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

	// ==================== VFX Methods ====================

	/** Spawn explosion effect at drone location */
	void SpawnExplosionEffect();

	/** Spawn muzzle flash effect */
	void SpawnMuzzleFlashEffect();

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
