// EMFProjectile.h
// Electromagnetic field projectile - interacts with EMF system as a charged particle

#pragma once

#include "CoreMinimal.h"
#include "ShooterProjectile.h"
#include "EMFProjectile.generated.h"

class UEMF_FieldComponent;
class UNiagaraSystem;

/**
 * Projectile with electromagnetic properties.
 *
 * Features:
 * - Has charge and mass (via UEMF_FieldComponent)
 * - Acts as a point charge in the EMF field
 * - Can be affected by external electromagnetic fields (attraction/repulsion)
 * - Damage and effects can be linked to charge magnitude
 *
 * Future expansion:
 * - Charge based on player's excess charge
 * - Transfer charge to hit target
 * - Charge-dependent damage scaling
 */
UCLASS()
class POLARITY_API AEMFProjectile : public AShooterProjectile
{
	GENERATED_BODY()

public:
	AEMFProjectile();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

public:
	// ==================== EMF Components ====================

	/** EMF Field Component - provides charge and mass properties (source of truth) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	// ==================== EMF Settings ====================

	/** Default charge of the projectile (can be overridden at spawn time) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge")
	float DefaultCharge = 10.0f;

	/** Default mass of the projectile (affects how much it's influenced by fields) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge")
	float DefaultMass = 1.0f;

	/** If true, projectile velocity is affected by external electromagnetic fields */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Physics")
	bool bAffectedByExternalFields = true;

	/** Maximum EM force that can be applied (prevents extreme accelerations) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Physics")
	float MaxEMForce = 100000.0f;

	/** Draw debug arrows for EM forces */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Debug")
	bool bDrawDebugForces = false;

	/** Log EM force calculations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Debug")
	bool bLogEMForces = false;

	// ==================== Charge-Based VFX ====================

	/** Trail VFX for positive charge projectiles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|VFX")
	TObjectPtr<UNiagaraSystem> PositiveTrailVFX;

	/** Trail VFX for negative charge projectiles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|VFX")
	TObjectPtr<UNiagaraSystem> NegativeTrailVFX;

	/** Explosion VFX for positive charge projectiles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|VFX")
	TObjectPtr<UNiagaraSystem> PositiveExplosionVFX;

	/** Explosion VFX for negative charge projectiles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|VFX")
	TObjectPtr<UNiagaraSystem> NegativeExplosionVFX;

	// ==================== Force Filtering ====================

	/** Multiplier for forces from NPC sources */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float NPCForceMultiplier = 1.0f;

	/** Multiplier for forces from Player sources */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float PlayerForceMultiplier = 1.0f;

	/** Multiplier for forces from Projectile sources */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float ProjectileForceMultiplier = 1.0f;

	/** Multiplier for forces from Environment sources */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float EnvironmentForceMultiplier = 1.0f;

	/** Multiplier for forces from Physics Prop sources */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float PhysicsPropForceMultiplier = 1.0f;

	/** Multiplier for forces from Unknown/unset sources */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float UnknownForceMultiplier = 1.0f;

	// ==================== Charge-Based Damage (Future) ====================

	/** Enable charge-based damage scaling */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Damage")
	bool bUseChargeDamageScaling = false;

	/** Damage multiplier per unit of charge (Damage = BaseDamage * (1 + ChargeMultiplier * Charge)) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Damage", meta = (EditCondition = "bUseChargeDamageScaling", ClampMin = "0.0"))
	float ChargeDamageMultiplier = 0.1f;

	/** Maximum damage multiplier from charge */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Damage", meta = (EditCondition = "bUseChargeDamageScaling", ClampMin = "1.0"))
	float MaxChargeDamageMultiplier = 3.0f;

	// ==================== Charge Transfer (Future) ====================

	/** Transfer charge to hit target on impact */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge Transfer")
	bool bTransferChargeOnHit = true;

	/** Percentage of projectile charge to transfer (0.0 - 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge Transfer", meta = (EditCondition = "bTransferChargeOnHit", ClampMin = "0.0", ClampMax = "1.0"))
	float ChargeTransferRatio = 0.5f;

	/** If true, neutralize opposite charges instead of adding them */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge Transfer", meta = (EditCondition = "bTransferChargeOnHit"))
	bool bNeutralizeOppositeCharges = true;

	// ==================== Public API ====================

	/** Set the charge of this projectile (call before or after spawn) */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetProjectileCharge(float NewCharge);

	/** Get current charge of this projectile */
	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetProjectileCharge() const;

	/** Set the mass of this projectile */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetProjectileMass(float NewMass);

	/** Get current mass of this projectile */
	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetProjectileMass() const;

	/** Initialize projectile with charge from player's excess charge (future implementation) */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void InitializeFromPlayerCharge(AActor* PlayerActor, float ChargeAmount);

protected:
	/** Override hit processing to add EMF effects */
	virtual void ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& HitLocation, const FVector& HitDirection) override;

	/** Override to reset EMF-specific state for pool reuse */
	virtual void ResetProjectileState() override;

	/** Override to spawn charge-based trail VFX */
	void SpawnChargeBasedTrailVFX();

	/** Spawn charge-based explosion VFX */
	void SpawnChargeBasedExplosionVFX(const FVector& Location);

	/** Get appropriate VFX based on current charge sign */
	UNiagaraSystem* GetChargeBasedVFX(UNiagaraSystem* PositiveVFX, UNiagaraSystem* NegativeVFX) const;

	/** Calculate damage with charge scaling */
	float CalculateChargeDamage() const;

	/** Transfer charge to hit actor if applicable */
	void TransferChargeToActor(AActor* HitActor);

	/** Apply electromagnetic forces to projectile velocity */
	void ApplyEMForces(float DeltaTime);

	/** Apply EMF-specific hit effects */
	UFUNCTION(BlueprintImplementableEvent, Category = "EMF", meta = (DisplayName = "On EMF Hit"))
	void BP_OnEMFHit(AActor* HitActor, float ProjectileCharge, const FHitResult& Hit);
};
