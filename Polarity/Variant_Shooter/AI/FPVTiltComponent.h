// FPVTiltComponent.h
// Visual mesh tilt for FPV drone flight — pitch, roll, yaw with wobble and overshoot.
// Does NOT affect movement. Purely cosmetic rotation applied to a StaticMeshComponent.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FPVTiltComponent.generated.h"

/**
 * Manages visual rotation of a drone mesh to simulate FPV flight characteristics.
 *
 * Pitch correlates with speed (faster = more forward tilt).
 * Roll correlates with lateral acceleration (turning = banking).
 * Yaw lags behind pitch/roll for authentic FPV feel.
 * Dual-sine wobble adds per-instance micro-corrections.
 * Slightly underdamped spring produces 2-4% overshoot on angle changes.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class POLARITY_API UFPVTiltComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UFPVTiltComponent();

	// ==================== Initialization ====================

	/**
	 * Initialize the tilt system with target mesh and flight parameters.
	 * @param InMesh - The mesh component whose relative rotation will be driven
	 * @param InMaxSpeed - Maximum flight speed (for pitch normalization)
	 * @param InSeed - Per-instance random seed for wobble desynchronization
	 */
	void Initialize(UStaticMeshComponent* InMesh, float InMaxSpeed, uint32 InSeed);

	// ==================== Movement Input ====================

	/**
	 * Feed current movement state each frame. Call from owning actor's Tick.
	 * @param CurrentSpeed - Current scalar speed
	 * @param Velocity - Current velocity vector
	 * @param Acceleration - Current acceleration vector (used for roll banking)
	 */
	void SetMovementState(float CurrentSpeed, const FVector& Velocity, const FVector& Acceleration);

	// ==================== Tilt Parameters ====================

	/** Maximum forward pitch angle (degrees) at max speed */
	UPROPERTY(EditAnywhere, Category = "FPV Tilt|Pitch", meta = (ClampMin = "0", ClampMax = "90"))
	float MaxPitchAngle = 70.0f;

	/** Interpolation speed for pitch changes */
	UPROPERTY(EditAnywhere, Category = "FPV Tilt|Pitch", meta = (ClampMin = "0.1"))
	float PitchInterpSpeed = 10.0f;

	/** Maximum roll angle (degrees) during turns */
	UPROPERTY(EditAnywhere, Category = "FPV Tilt|Roll", meta = (ClampMin = "0", ClampMax = "90"))
	float MaxRollAngle = 70.0f;

	/** Interpolation speed for roll changes */
	UPROPERTY(EditAnywhere, Category = "FPV Tilt|Roll", meta = (ClampMin = "0.1"))
	float RollInterpSpeed = 12.0f;

	/** Multiplier converting lateral acceleration to roll angle */
	UPROPERTY(EditAnywhere, Category = "FPV Tilt|Roll", meta = (ClampMin = "0.0"))
	float BankMultiplier = 0.07f;

	/** Interpolation speed for yaw (intentionally slower than pitch/roll) */
	UPROPERTY(EditAnywhere, Category = "FPV Tilt|Yaw", meta = (ClampMin = "0.1"))
	float YawInterpSpeed = 5.0f;

	// ==================== Wobble (Micro-Corrections) ====================

	/** Primary wobble amplitude (degrees) */
	UPROPERTY(EditAnywhere, Category = "FPV Tilt|Wobble", meta = (ClampMin = "0"))
	float WobbleAmplitude = 1.5f;

	/** Primary wobble frequency (Hz) */
	UPROPERTY(EditAnywhere, Category = "FPV Tilt|Wobble", meta = (ClampMin = "0.1"))
	float WobbleFrequency = 5.0f;

	/** Secondary wobble amplitude (degrees) — irrational frequency ratio avoids repeating patterns */
	UPROPERTY(EditAnywhere, Category = "FPV Tilt|Wobble", meta = (ClampMin = "0"))
	float SecondaryAmplitude = 0.8f;

	/** Secondary wobble frequency (Hz) — should be irrational relative to primary */
	UPROPERTY(EditAnywhere, Category = "FPV Tilt|Wobble", meta = (ClampMin = "0.1"))
	float SecondaryFrequency = 3.7f;

	/** Wobble amplification at max speed (1.0 = no change, 2.0 = double wobble at full speed) */
	UPROPERTY(EditAnywhere, Category = "FPV Tilt|Wobble", meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float SpeedWobbleMultiplier = 2.0f;

	// ==================== Overshoot ====================

	/** Spring damping factor (< 1.0 = overshoot, 1.0 = critical damping). ~0.95 gives 2-4% overshoot. */
	UPROPERTY(EditAnywhere, Category = "FPV Tilt|Overshoot", meta = (ClampMin = "0.8", ClampMax = "1.0"))
	float SpringDamping = 0.95f;

private:

	// ==================== Internal State ====================

	/** Cached mesh to rotate */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> TargetMesh;

	/** Max speed used for pitch normalization */
	float MaxSpeed = 1200.0f;

	/** Per-instance random stream for wobble desync */
	FRandomStream WobbleRandom;

	/** Time offset per-instance so swarm drones don't wobble in sync */
	float WobbleTimeOffset = 0.0f;

	/** Accumulated time for wobble calculation */
	float AccumulatedTime = 0.0f;

	// Spring state (underdamped for overshoot)
	FRotator CurrentAngles = FRotator::ZeroRotator;
	FRotator AngularVelocity = FRotator::ZeroRotator;
	FRotator TargetAngles = FRotator::ZeroRotator;

	/** Previous velocity for deceleration detection */
	FVector PreviousVelocity = FVector::ZeroVector;

	bool bInitialized = false;

	// ==================== Internal Methods ====================

	/** Calculate target pitch from speed */
	float CalculateTargetPitch(float CurrentSpeed, bool bDecelerating) const;

	/** Calculate target roll from lateral acceleration */
	float CalculateTargetRoll(const FVector& Velocity, const FVector& Acceleration) const;

	/** Calculate wobble offset for a given axis */
	float CalculateWobble(float Time, float AxisOffset) const;

	/** Apply underdamped spring to smoothly approach target with overshoot */
	void UpdateSpring(float DeltaTime);
};
