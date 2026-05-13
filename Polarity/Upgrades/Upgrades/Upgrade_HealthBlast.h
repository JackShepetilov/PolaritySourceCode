// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_HealthBlast.generated.h"

class UUpgradeDefinition_HealthBlast;
class UChargeAnimationComponent;
class UUpgradeManagerComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;
class USphereComponent;
class UNiagaraSystem;
class USoundBase;

// ==================== Delegate for UI ====================
// Kept for legacy widget subscribers. Identical signature to
// UUpgradeManagerComponent::FOnStoredHealthPickupsChanged — this component
// re-broadcasts the manager event so existing Blueprints don't need rewiring.

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStoredPickupsChanged, int32, CurrentCount, int32, MaxCount);

// ==================== Health Blast Projectile ====================

/**
 * Simple projectile actor for the Health Blast upgrade.
 * Flies in a straight line, deals damage + knockback on overlap, then destroys itself.
 */
UCLASS(NotBlueprintable)
class POLARITY_API AHealthBlastProjectile : public AActor
{
	GENERATED_BODY()

public:

	AHealthBlastProjectile();

	/** Initialize the projectile after spawn */
	void InitProjectile(float Speed, float Lifetime, float InDamage, float InDamageRadius,
		float InTargetKnockback, UNiagaraSystem* InHitVFX, USoundBase* InHitSound,
		UStaticMesh* InMesh, const FVector& InMeshScale, float InCollisionRadius);

private:

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	/** Damage this projectile deals */
	float Damage = 0.0f;

	/** Radius for damage application (0 = direct hit only) */
	float DamageRadius = 0.0f;

	/** Knockback force applied to hit target */
	float TargetKnockback = 0.0f;

	/** VFX to spawn on hit */
	TObjectPtr<UNiagaraSystem> HitVFX;

	/** Sound to play on hit */
	TObjectPtr<USoundBase> HitSound;

	/** Track which actors we already hit (prevent double damage) */
	TSet<AActor*> HitActors;

	UFUNCTION()
	void OnProjectileOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnProjectileBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity);

	void OnLifetimeExpired();

	FTimerHandle LifetimeTimer;
};

// ==================== Health Blast Upgrade Component ====================

/**
 * Upgrade logic: stores health pickups collected at full HP,
 * fires them as a shotgun blast on empty capture.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Health Blast"))
class POLARITY_API UUpgrade_HealthBlast : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_HealthBlast();

	/** Current number of stored health pickups (reads from the shared UpgradeManager pool) */
	UFUNCTION(BlueprintPure, Category = "Health Blast")
	int32 GetStoredPickups() const;

	/** Broadcast when stored pickup count changes (for UI). Re-broadcasts the shared pool's event. */
	UPROPERTY(BlueprintAssignable, Category = "Health Blast")
	FOnStoredPickupsChanged OnStoredPickupsChanged;

protected:

	// ==================== Lifecycle ====================

	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;

	// ==================== Event Hooks ====================

	virtual void OnHealthPickupCollectedAtFullHP() override;

private:

	/** Typed cached definition */
	TWeakObjectPtr<UUpgradeDefinition_HealthBlast> CachedDef;

	/** Cached reference to ChargeAnimationComponent */
	TWeakObjectPtr<UChargeAnimationComponent> CachedChargeComp;

	/** Cached reference to the owning character's UpgradeManager (host of the shared pool) */
	TWeakObjectPtr<UUpgradeManagerComponent> CachedUpgradeManager;

	/** Whether blast is on cooldown */
	bool bOnCooldown = false;

	/** Timer for empty capture detection */
	FTimerHandle EmptyCaptureTimer;

	/** Timer for cooldown after firing */
	FTimerHandle CooldownTimer;

	// ==================== Channeling Callbacks ====================

	/** Called when player starts channeling */
	UFUNCTION()
	void OnChannelingStarted();

	/** Called when player captures something (cancel empty capture timer) */
	UFUNCTION()
	void OnPropCaptured(AActor* CapturedActor);

	/** Called when channeling ends (cancel empty capture timer) */
	UFUNCTION()
	void OnChannelingEnded();

	/** Re-broadcast the shared pool change to OnStoredPickupsChanged (UI compatibility) */
	UFUNCTION()
	void HandleSharedPoolChanged(int32 CurrentCount, int32 MaxCount);

	/** Called when empty capture timer fires — execute the blast */
	void OnEmptyCaptureTimerFired();

	/** Fire all stored health pickups as a shotgun blast */
	void FireHealthBlast();

	/** Get a random direction within a cone around the forward direction */
	FVector GetRandomConeDirection(const FVector& Forward, float HalfAngleDegrees) const;
};
