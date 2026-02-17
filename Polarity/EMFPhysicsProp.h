// EMFPhysicsProp.h
// Physics-simulated prop that integrates with the EMF system
// Receives/gives charge, affected by EM forces, can be captured by channeling, deals impact damage

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Variant_Shooter/ShooterDummyInterface.h"
#include "EMF_PluginBPLibrary.h"
#include "EMFPhysicsProp.generated.h"

class UEMF_FieldComponent;
class AEMFChannelingPlateActor;
class AShooterNPC;
class UNiagaraSystem;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPropDeath, AEMFPhysicsProp*, Prop, AActor*, Killer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPropDamaged, AEMFPhysicsProp*, Prop, float, Damage, AActor*, DamageCauser);

/**
 * Physics-simulated prop with full EMF system integration.
 *
 * Features:
 * - Receives charge from melee hits and laser ionization
 * - Affected by electromagnetic forces (like enemies and projectiles)
 * - Can be captured by player's channeling plate
 * - Deals kinetic and EMF damage to NPCs on impact
 * - Compatible with future destructibility (SceneComponent root)
 */
UCLASS(Blueprintable)
class POLARITY_API AEMFPhysicsProp : public AActor, public IShooterDummyTarget
{
	GENERATED_BODY()

public:
	AEMFPhysicsProp();

	// ==================== Components ====================

	/** Root scene component (stable root for future mesh swaps) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Physics mesh (simulates physics, generates hit events) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> PropMesh;

	/** EMF field component (charge storage + registry) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	// ==================== EMF Settings ====================

	/** Default charge (0 = starts uncharged) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge")
	float DefaultCharge = 0.0f;

	/** Default mass (affects EMF force response and physics weight) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge")
	float DefaultMass = 10.0f;

	/** If true, prop velocity is affected by external electromagnetic fields */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Physics")
	bool bAffectedByExternalFields = true;

	/** Maximum EM force that can be applied (prevents extreme accelerations) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Physics")
	float MaxEMForce = 100000.0f;

	/** Maximum distance to consider EMF sources */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Physics", meta = (ClampMin = "100.0", Units = "cm"))
	float MaxSourceDistance = 10000.0f;

	// ==================== Force Filtering ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float PlayerForceMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float NPCForceMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float ProjectileForceMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float EnvironmentForceMultiplier = 1.0f;

	/** Default OFF to prevent prop-prop EMF chaos */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float PhysicsPropForceMultiplier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float UnknownForceMultiplier = 1.0f;

	// ==================== Health ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "1.0"))
	float MaxHP = 100.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Health")
	float CurrentHP = 100.0f;

	// ==================== Collision Damage ====================

	/** Enable kinetic/EMF damage to NPCs on impact */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage")
	bool bDealCollisionDamage = true;

	/** Minimum speed to deal kinetic damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage", meta = (ClampMin = "0"))
	float CollisionVelocityThreshold = 800.0f;

	/** Kinetic damage per 100 units of speed above threshold */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage", meta = (ClampMin = "0"))
	float CollisionDamagePerVelocity = 10.0f;

	/** Base EMF damage when opposite-charged prop hits NPC */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage|EMF", meta = (ClampMin = "0"))
	float EMFProximityDamage = 10.0f;

	/** Minimum time between collision damage events */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float CollisionDamageCooldown = 0.2f;

	// ==================== Collision Effects ====================

	/** Sound to play on impact with NPC */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage|Effects")
	TObjectPtr<USoundBase> ImpactSound;

	/** VFX to spawn on EMF discharge impact */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage|Effects")
	TObjectPtr<UNiagaraSystem> EMFDischargeVFX;

	/** Scale for EMF discharge VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision Damage|Effects", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float EMFDischargeVFXScale = 1.0f;

	// ==================== Melee Charge Transfer ====================

	/** Charge added to prop when hit by melee (opposite sign to attacker's charge) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge|Melee", meta = (ClampMin = "-100.0", ClampMax = "100.0"))
	float ChargeChangeOnMeleeHit = -10.0f;

	/** If true, melee hits grant stable charge to the player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge|Melee")
	bool bGrantsStableCharge = false;

	/** Amount of stable charge per melee hit (for player) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge|Melee", meta = (ClampMin = "0.0", EditCondition = "bGrantsStableCharge"))
	float StableChargePerHit = 1.0f;

	/** Bonus charge on kill (for player) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge|Melee", meta = (ClampMin = "0.0", EditCondition = "bGrantsStableCharge"))
	float KillChargeBonus = 0.0f;

	// ==================== Channeling Capture ====================

	/** Can this prop be captured by the channeling plate? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture")
	bool bCanBeCaptured = true;

	/** Viscosity coefficient (damping strength). Higher = faster capture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (ClampMin = "0.0", ClampMax = "50.0", EditCondition = "bCanBeCaptured"))
	float ViscosityCoefficient = 10.0f;

	/** Radius within which viscous capture activates */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (ClampMin = "50.0", Units = "cm", EditCondition = "bCanBeCaptured"))
	float CaptureRadius = 500.0f;

	/** Counteract gravity when captured */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (EditCondition = "bCanBeCaptured"))
	bool bCounterGravityWhenCaptured = true;

	/** Gravity counteraction strength (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bCanBeCaptured"))
	float GravityCounterStrength = 1.0f;

	/** Minimum CaptureStrength to stay captured */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bCanBeCaptured"))
	float CaptureMinStrength = 0.05f;

	/** Time below CaptureMinStrength before auto-release */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channeling Capture", meta = (ClampMin = "0.1", ClampMax = "5.0", EditCondition = "bCanBeCaptured"))
	float CaptureReleaseTimeout = 0.5f;

	// ==================== Debug ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Debug")
	bool bDrawDebugForces = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Debug")
	bool bLogEMForces = false;

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnPropDeath OnPropDeath;

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnPropDamaged OnPropDamaged;

	// ==================== Public API ====================

	/** Get current charge */
	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetCharge() const;

	/** Set charge directly */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetCharge(float NewCharge);

	/** Get EMF mass */
	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetPropMass() const;

	/** Set EMF mass (also updates physics body mass) */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetPropMass(float NewMass);

	/** Is this prop dead? */
	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return bIsDead; }

	/** Get health percentage (0-1) */
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealthPercent() const { return MaxHP > 0.0f ? CurrentHP / MaxHP : 0.0f; }

	// ==================== Channeling Capture API ====================

	/** Mark this prop as captured by the given plate */
	UFUNCTION(BlueprintCallable, Category = "Channeling Capture")
	void SetCapturedByPlate(AEMFChannelingPlateActor* Plate);

	/** Release this prop from capture */
	UFUNCTION(BlueprintCallable, Category = "Channeling Capture")
	void ReleasedFromCapture();

	/** Is this prop currently captured? */
	UFUNCTION(BlueprintPure, Category = "Channeling Capture")
	bool IsCapturedByPlate() const { return CapturingPlate.IsValid(); }

	/** Detach from plate without fully releasing (for plate swap during reverse channeling) */
	void DetachFromPlate();

	// ==================== IShooterDummyTarget Interface ====================

	virtual bool GrantsStableCharge_Implementation() const override;
	virtual float GetStableChargeAmount_Implementation() const override;
	virtual float GetKillChargeBonus_Implementation() const override;
	virtual bool IsDummyDead_Implementation() const override;

	// ==================== AActor Overrides ====================

	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	bool bIsDead = false;
	float LastCollisionDamageTime = -1.0f;

	// ==================== Channeling Capture State ====================

	UPROPERTY()
	TWeakObjectPtr<AEMFChannelingPlateActor> CapturingPlate;

	FVector PreviousPlatePosition = FVector::ZeroVector;
	bool bHasPreviousPlatePosition = false;
	float WeakCaptureTimer = 0.0f;

	// ==================== Internal Methods ====================

	/** Apply electromagnetic forces from all EMF sources */
	void ApplyEMForces(float DeltaTime);

	/** Apply viscous capture forces when held by channeling plate */
	void UpdateCaptureForces(float DeltaTime);

	/** Handle collision with other actors (damage to NPCs) */
	UFUNCTION()
	void OnPropHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/** Called when HP reaches zero */
	void Die(AActor* Killer);

	/** Get force multiplier for a given source owner type */
	float GetForceMultiplierForOwnerType(EEMSourceOwnerType OwnerType) const;

	/** Check if source has effectively zero charge/field strength */
	static bool IsSourceEffectivelyZero(const FEMSourceDescription& Source);
};
