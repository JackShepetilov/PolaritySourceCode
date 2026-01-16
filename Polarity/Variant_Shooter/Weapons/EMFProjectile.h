// EMFProjectile.h
// Electromagnetic field projectile - interacts with EMF system as a charged particle

#pragma once

#include "CoreMinimal.h"
#include "ShooterProjectile.h"
#include "EMFProjectile.generated.h"

class UEMF_FieldComponent;
class UEMFVelocityModifier;

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

public:
	// ==================== EMF Components ====================

	/** EMF Field Component - provides charge and mass properties */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	/** EMF Velocity Modifier - allows projectile to be affected by external fields */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<UEMFVelocityModifier> VelocityModifier;

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
	bool bTransferChargeOnHit = false;

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

	/** Calculate damage with charge scaling */
	float CalculateChargeDamage() const;

	/** Transfer charge to hit actor if applicable */
	void TransferChargeToActor(AActor* HitActor);

	/** Apply EMF-specific hit effects */
	UFUNCTION(BlueprintImplementableEvent, Category = "EMF", meta = (DisplayName = "On EMF Hit"))
	void BP_OnEMFHit(AActor* HitActor, float ProjectileCharge, const FHitResult& Hit);
};
