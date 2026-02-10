// ShooterWeapon_Laser.h
// Continuous beam weapon that ionizes targets (applies positive charge)

#pragma once

#include "CoreMinimal.h"
#include "ShooterWeapon.h"
#include "ShooterWeapon_Laser.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UAudioComponent;
class UEMFVelocityModifier;
class UEMF_FieldComponent;

/**
 * Laser weapon - continuous beam that deals damage and ionizes targets.
 *
 * The beam is active while the fire button is held. Each tick:
 * - Line trace from muzzle in aim direction
 * - Deal DPS-based damage to hit target
 * - Add positive charge to targets with EMF components (ionization)
 * - Update Niagara beam endpoints
 *
 * Reflections will be added later.
 */
UCLASS()
class POLARITY_API AShooterWeapon_Laser : public AShooterWeapon
{
	GENERATED_BODY()

public:
	AShooterWeapon_Laser();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void Fire() override;

	// ==================== Laser Damage ====================

	/** Damage dealt per second while beam is hitting a target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Damage", meta = (ClampMin = "0.0"))
	float DamagePerSecond = 50.0f;

	/** Damage type for laser hits */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Damage")
	TSubclassOf<UDamageType> LaserDamageType;

	// ==================== Laser Ionization ====================

	/** Charge added per second to hit targets (always positive - ionization) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Ionization", meta = (ClampMin = "0.0"))
	float IonizationChargePerSecond = 5.0f;

	/** Maximum positive charge that ionization can apply */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Ionization", meta = (ClampMin = "0.0"))
	float MaxIonizationCharge = 20.0f;

	// ==================== Laser Beam ====================

	/** Maximum beam range (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Beam", meta = (ClampMin = "100.0"))
	float MaxBeamRange = 5000.0f;

	// ==================== Laser VFX ====================

	/** Niagara system for the beam. Must have BeamStart/BeamEnd vector parameters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|VFX")
	TObjectPtr<UNiagaraSystem> LaserBeamFX;

	/** Scale_E parameter - controls beam visual thickness */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|VFX", meta = (ClampMin = "0.1"))
	float BeamScaleE = 4.0f;

	/** Scale_E_Mesh parameter - mesh scale for beam effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|VFX")
	FVector BeamScaleEMesh = FVector(1.0f, 1.0f, 1.0f);

	/** ColorEnergy - beam color/energy parameter */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|VFX")
	FLinearColor LaserColorEnergy = FLinearColor(0.2f, 0.5f, 1.0f, 1.0f);

	/** Niagara system for impact point on surfaces */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|VFX")
	TObjectPtr<UNiagaraSystem> LaserImpactFX;

	// ==================== Laser SFX ====================

	/** Sound played once when beam starts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|SFX")
	TObjectPtr<USoundBase> BeamStartSound;

	/** Looping sound while beam is active */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|SFX")
	TObjectPtr<USoundBase> BeamLoopSound;

	/** Sound played once when beam stops */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|SFX")
	TObjectPtr<USoundBase> BeamStopSound;

	// ==================== Laser Heat ====================

	/** Heat added per second while firing (instead of per-shot) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Heat", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float HeatPerSecond = 0.15f;

private:

	// ==================== Runtime State ====================

	/** True while the beam is actively firing */
	bool bBeamActive = false;

	/** The Niagara component for the active beam */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveBeamComponent;

	/** The Niagara component for impact effect */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveImpactComponent;

	/** Audio component for the looping beam sound */
	UPROPERTY()
	TObjectPtr<UAudioComponent> BeamLoopAudioComponent;

	/** Currently hit actor (for tracking continuous damage on same target) */
	TWeakObjectPtr<AActor> CurrentHitActor;

	// ==================== Internal Methods ====================

	/** Perform the beam trace and return hit result. Returns true if something was hit. */
	bool PerformBeamTrace(FHitResult& OutHit, FVector& OutBeamStart, FVector& OutBeamEnd) const;

	/** Apply continuous damage to the hit actor */
	void ApplyBeamDamage(const FHitResult& Hit, float DeltaTime);

	/** Apply ionization (positive charge) to the hit actor */
	void ApplyIonization(AActor* Target, float DeltaTime);

	/** Activate beam visuals and audio */
	void ActivateBeam();

	/** Deactivate beam visuals and audio */
	void DeactivateBeam();

	/** Update beam Niagara component endpoints */
	void UpdateBeamVFX(const FVector& Start, const FVector& End);

	/** Update impact VFX position, or hide if no surface hit */
	void UpdateImpactVFX(bool bHitSurface, const FVector& Location, const FVector& Normal);
};
