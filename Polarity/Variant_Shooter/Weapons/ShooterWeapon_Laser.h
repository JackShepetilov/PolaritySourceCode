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

/** Phase of the Second Harmonic Generation ability */
UENUM()
enum class ESecondHarmonicPhase : uint8
{
	None,
	VerticalSweep,
	HorizontalSweep
};

/**
 * Laser weapon - continuous beam that deals damage and ionizes targets.
 *
 * The beam is active while the fire button is held. Each tick:
 * - Line trace from muzzle in aim direction
 * - Deal DPS-based damage to hit target
 * - Add positive charge to targets with EMF components (ionization)
 * - Update Niagara beam endpoints
 *
 * Secondary action (ADS button) triggers Second Harmonic Generation ability:
 * - Two beams sweep from top/bottom to center (vertical phase)
 * - Then two beams sweep from left/right to center (horizontal phase)
 * - Each beam deals one-time massive damage to every target it touches
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
	virtual bool OnSecondaryAction() override;

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

	// ==================== Second Harmonic Generation ====================

	/** One-time damage dealt by each sweep beam on contact */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Second Harmonic", meta = (ClampMin = "0.0"))
	float SecondHarmonicDamage = 500.0f;

	/** Damage type for second harmonic hits. If not set, uses LaserDamageType. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Second Harmonic")
	TSubclassOf<UDamageType> SecondHarmonicDamageType;

	/** Starting angle (degrees) of sweep beams from center aim direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Second Harmonic", meta = (ClampMin = "1.0", ClampMax = "90.0"))
	float InitialSweepAngleDeg = 30.0f;

	/** Duration of vertical sweep phase (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Second Harmonic", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float VerticalSweepDuration = 0.6f;

	/** Duration of horizontal sweep phase (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Second Harmonic", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float HorizontalSweepDuration = 0.6f;

	/** Cooldown between ability uses (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Second Harmonic", meta = (ClampMin = "0.0"))
	float SecondHarmonicCooldown = 10.0f;

	/** Color for the second harmonic beams (different from main laser) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Second Harmonic")
	FLinearColor SecondHarmonicColor = FLinearColor(0.1f, 1.0f, 0.2f, 1.0f);

	/** Optional different Niagara system for harmonic beams. If null, uses LaserBeamFX. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Second Harmonic")
	TObjectPtr<UNiagaraSystem> SecondHarmonicBeamFX;

private:

	// ==================== Main Beam Runtime State ====================

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

	// ==================== Second Harmonic Runtime State ====================

	/** Current phase of the Second Harmonic ability */
	ESecondHarmonicPhase CurrentHarmonicPhase = ESecondHarmonicPhase::None;

	/** Time elapsed in the current sweep phase */
	float HarmonicPhaseElapsedTime = 0.0f;

	/** World time of last ability use (for cooldown) */
	float LastHarmonicUseTime = -100.0f;

	/** Whether main beam was active before ability started (to restore after) */
	bool bMainBeamWasActive = false;

	/** Actors already hit by beam A in current phase (one-hit-per-target) */
	TSet<TWeakObjectPtr<AActor>> HitActorsBeamA;

	/** Actors already hit by beam B in current phase (one-hit-per-target) */
	TSet<TWeakObjectPtr<AActor>> HitActorsBeamB;

	/** Niagara component for sweep beam A (top/left) */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveHarmonicBeamA;

	/** Niagara component for sweep beam B (bottom/right) */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveHarmonicBeamB;

	// ==================== Main Beam Methods ====================

	/** Perform the beam trace and return hit result. Returns true if something was hit. bOutHitPawn is true only if a pawn was hit (not a wall). */
	bool PerformBeamTrace(FHitResult& OutHit, FVector& OutBeamStart, FVector& OutBeamEnd, bool& bOutHitPawn) const;

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

	// ==================== Second Harmonic Methods ====================

	/** Start the Second Harmonic ability (vertical sweep phase) */
	void ActivateSecondHarmonic();

	/** Main tick logic for the ability — traces, damage, VFX */
	void UpdateSecondHarmonic(float DeltaTime);

	/** Transition from vertical to horizontal sweep */
	void TransitionToHorizontalSweep();

	/** End the ability and optionally restore main beam */
	void DeactivateSecondHarmonic();

	/** Perform line trace for a single sweep beam in a given direction. Same two-trace approach as main beam. */
	bool PerformSweepTrace(const FVector& Direction, FHitResult& OutHit, FVector& OutBeamStart, FVector& OutBeamEnd, bool& bOutHitPawn) const;

	/** Spawn the two harmonic beam Niagara components */
	void SpawnHarmonicBeams();

	/** Destroy the two harmonic beam Niagara components */
	void DestroyHarmonicBeams();

	/** Update a single harmonic beam's Niagara parameters */
	void UpdateHarmonicBeamVFX(UNiagaraComponent* Comp, const FVector& Start, const FVector& End);
};
