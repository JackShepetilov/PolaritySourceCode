// EMFProjectile.h
// Electromagnetic field projectile - interacts with EMF system as a charged particle

#pragma once

#include "CoreMinimal.h"
#include "ShooterProjectile.h"
#include "EMFProjectile.generated.h"

class UEMF_FieldComponent;
class UNiagaraSystem;
class AShooterNPC;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnProjectileCriticalVelocityImpact, AEMFProjectile*, Projectile, FVector, Location, float, Speed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnProjectileFired, float, Charge);

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

	// ==================== LOS Shielding ====================

	/** Skip EMF sources blocked by geometry (walls, floors, etc.).
	 *  Uses a single line trace per source. Only sources that pass multiplier filtering are checked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|LOS Shielding")
	bool bEnableLOSShielding = false;

	/** Trace channel for LOS checks. Use Visibility for simple wall blocking,
	 *  or a custom channel for fine-grained control over what blocks EMF. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|LOS Shielding", meta = (EditCondition = "bEnableLOSShielding"))
	TEnumAsByte<ECollisionChannel> LOSTraceChannel = ECC_Visibility;

	/** Draw debug lines for LOS traces (green = visible, red = blocked) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|LOS Shielding", meta = (EditCondition = "bEnableLOSShielding"))
	bool bDrawLOSDebug = false;

	// ==================== Charge-Based Homing ====================

	/** Enable single-target charge-scaled homing. When true, the projectile locks onto ONE
	 *  charged target and steers toward it (guaranteed hit), instead of summing field forces
	 *  from all sources (which can thread between enemies and miss everyone). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Homing")
	bool bUseChargeHoming = true;

	/** Only home onto targets whose charge is opposite to the projectile (classic attract-to-electrified).
	 *  When false, any charged target is eligible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Homing", meta = (EditCondition = "bUseChargeHoming"))
	bool bRequireOppositeCharge = true;

	/** Half-angle of the cone (relative to current velocity) used to acquire a homing target (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Homing", meta = (EditCondition = "bUseChargeHoming", ClampMin = "1.0", ClampMax = "180.0"))
	float HomingConeHalfAngle = 35.0f;

	/** Max range to acquire a homing target (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Homing", meta = (EditCondition = "bUseChargeHoming", ClampMin = "0.0"))
	float HomingMaxRange = 6000.0f;

	/** How often (s) the projectile re-evaluates the best target. 0 = every tick.
	 *  Re-acquisition is forced immediately if the locked target dies or becomes invalid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Homing", meta = (EditCondition = "bUseChargeHoming", ClampMin = "0.0"))
	float HomingRetargetInterval = 0.1f;

	/** Score multiplier favoring the currently-locked target, to prevent thrashing between targets (>= 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Homing", meta = (EditCondition = "bUseChargeHoming", ClampMin = "1.0"))
	float HomingStickiness = 1.5f;

	/** Homing acceleration per unit of |q_proj * q_target| (cm/s^2 per charge^2).
	 *  This is the "homing strength linked to charge" — bigger charge product = tighter, faster curve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Homing", meta = (EditCondition = "bUseChargeHoming", ClampMin = "0.0"))
	float HomingAccelPerChargeProduct = 4000.0f;

	/** Minimum homing acceleration once a target is locked. Set high enough that the turn radius
	 *  stays below engagement distance — this is what guarantees the hit even at low charge (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Homing", meta = (EditCondition = "bUseChargeHoming", ClampMin = "0.0"))
	float MinHomingAccel = 8000.0f;

	/** Maximum homing acceleration (cm/s^2) — caps the curve so high-charge shots don't orbit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Homing", meta = (EditCondition = "bUseChargeHoming", ClampMin = "0.0"))
	float MaxHomingAccel = 60000.0f;

	/** Actor tag that marks non-NPC actors as eligible homing targets (e.g. training dummies, props).
	 *  NPC with charge are always eligible — this tag is an additional opt-in for anything else. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Homing", meta = (EditCondition = "bUseChargeHoming"))
	FName HomingTargetTag = FName("HomingTarget");

	/** Virtual charge used for scoring tagged targets that have no UEMF_FieldComponent.
	 *  Acts as if the target has this much opposite charge for homing purposes.
	 *  Real charged NPCs with higher charge will still be preferred. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Homing", meta = (EditCondition = "bUseChargeHoming", ClampMin = "0.0"))
	float HomingTagVirtualCharge = 5.0f;

	/** Draw debug for homing target acquisition (line to locked target + accel/charge readout) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Homing")
	bool bDrawHomingDebug = false;

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

	// ==================== Critical Velocity ====================

	/** Speed (cm/s) at which projectile impact triggers arena-level critical destruction event */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Critical Velocity", meta = (ClampMin = "0.0"))
	float CriticalVelocity = 0.0f;

	/** Delegate fired when projectile hits at critical velocity */
	UPROPERTY(BlueprintAssignable, Category = "EMF|Critical Velocity")
	FOnProjectileCriticalVelocityImpact OnCriticalVelocityImpact;

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

	// ==================== Events ====================

	/** Fired when the projectile's charge is set (i.e. when actually fired, not on pool creation) */
	UPROPERTY(BlueprintAssignable, Category = "EMF|Events")
	FOnProjectileFired OnProjectileFired;

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

	/** Update single-target charge-scaled homing (called from Tick when bUseChargeHoming). */
	void UpdateChargeHoming(float DeltaTime);

	/** Select the best homing target by charge-weighted cone score, with stickiness toward the current target.
	 *  Accepts AShooterNPC (by charge) and any actor with HomingTargetTag (by virtual charge). */
	AActor* SelectHomingTarget() const;

	/** Currently locked homing target (weak — target may die or be pooled out from under us).
	 *  Can be AShooterNPC or any tagged actor (e.g. training dummy). */
	TWeakObjectPtr<AActor> CurrentHomingTarget;

	/** Time accumulator since the last retarget evaluation. */
	float TimeSinceRetarget = 0.0f;

	/** Diagnostic: log sources only once per projectile lifetime */
	bool bDiagnosticLogged = false;

	/** Apply EMF-specific hit effects */
	UFUNCTION(BlueprintImplementableEvent, Category = "EMF", meta = (DisplayName = "On EMF Hit"))
	void BP_OnEMFHit(AActor* HitActor, float ProjectileCharge, const FHitResult& Hit);
};
