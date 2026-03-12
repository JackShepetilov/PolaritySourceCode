// FPVTiltComponent.cpp

#include "FPVTiltComponent.h"
#include "Components/StaticMeshComponent.h"

UFPVTiltComponent::UFPVTiltComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // Driven by owning actor's Tick via SetMovementState
}

void UFPVTiltComponent::Initialize(UStaticMeshComponent* InMesh, float InMaxSpeed, uint32 InSeed)
{
	TargetMesh = InMesh;
	MaxSpeed = FMath::Max(InMaxSpeed, 1.0f);

	WobbleRandom.Initialize(InSeed);
	WobbleTimeOffset = WobbleRandom.FRandRange(0.0f, 100.0f);
	AccumulatedTime = WobbleTimeOffset;

	bInitialized = true;
}

void UFPVTiltComponent::SetMovementState(float CurrentSpeed, const FVector& Velocity, const FVector& Acceleration)
{
	if (!bInitialized || !TargetMesh)
	{
		return;
	}

	const float DeltaTime = GetWorld()->GetDeltaSeconds();
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	AccumulatedTime += DeltaTime;

	// Detect deceleration: speed decreasing
	const bool bDecelerating = Velocity.SizeSquared() < PreviousVelocity.SizeSquared() * 0.9f;
	PreviousVelocity = Velocity;

	// --- Calculate target angles ---

	// Pitch: forward tilt based on speed, negative when braking
	const float TargetPitch = CalculateTargetPitch(CurrentSpeed, bDecelerating);

	// Roll: bank into turns based on lateral acceleration
	const float TargetRoll = CalculateTargetRoll(Velocity, Acceleration);

	// Yaw: align mesh forward direction with velocity (just offset, not absolute)
	// Yaw is handled by the actor's rotation; tilt component only adds relative yaw lag
	// We keep tilt yaw at 0 and let the natural lag from lower interp speed create the effect
	const float TargetYaw = 0.0f;

	// --- Apply wobble noise (amplified at high speed) ---
	const float SpeedRatio = FMath::Clamp(CurrentSpeed / MaxSpeed, 0.0f, 1.0f);
	const float WobbleMult = FMath::Lerp(1.0f, SpeedWobbleMultiplier, SpeedRatio);

	const float WobblePitch = CalculateWobble(AccumulatedTime, 0.0f) * WobbleMult;
	const float WobbleRoll = CalculateWobble(AccumulatedTime, 1.7f) * WobbleMult;
	const float WobbleYaw = CalculateWobble(AccumulatedTime, 3.3f) * WobbleMult;

	// Set spring targets (tilt + wobble)
	TargetAngles.Pitch = TargetPitch + WobblePitch;
	TargetAngles.Roll = TargetRoll + WobbleRoll;
	TargetAngles.Yaw = TargetYaw + WobbleYaw;

	// --- Update spring (with overshoot) ---
	UpdateSpring(DeltaTime);

	// --- Apply to mesh ---
	TargetMesh->SetRelativeRotation(CurrentAngles);
}

float UFPVTiltComponent::CalculateTargetPitch(float CurrentSpeed, bool bDecelerating) const
{
	if (bDecelerating)
	{
		// Nose up when braking: -15 to -25 degrees based on deceleration
		const float SpeedRatio = FMath::Clamp(CurrentSpeed / MaxSpeed, 0.0f, 1.0f);
		return -MaxPitchAngle * 0.35f * (1.0f - SpeedRatio);
	}

	// Forward pitch: power curve gives quick ramp at low speeds, plateau at high
	const float SpeedRatio = FMath::Clamp(CurrentSpeed / MaxSpeed, 0.0f, 1.0f);
	return MaxPitchAngle * FMath::Pow(SpeedRatio, 0.7f);
}

float UFPVTiltComponent::CalculateTargetRoll(const FVector& Velocity, const FVector& Acceleration) const
{
	if (Velocity.IsNearlyZero(1.0f))
	{
		return 0.0f;
	}

	// Lateral acceleration = cross product of velocity direction and acceleration
	// Positive = turning right, Negative = turning left
	const FVector VelDir = Velocity.GetSafeNormal();
	const FVector HorizAccel = FVector(Acceleration.X, Acceleration.Y, 0.0f);

	// Cross product Z component gives lateral acceleration magnitude with sign
	const float LateralAccel = FVector::CrossProduct(VelDir, HorizAccel).Z;

	return FMath::Clamp(LateralAccel * BankMultiplier, -MaxRollAngle, MaxRollAngle);
}

float UFPVTiltComponent::CalculateWobble(float Time, float AxisOffset) const
{
	// Dual-sine wobble with irrational frequency ratio to avoid repeating patterns
	const float Primary = WobbleAmplitude * FMath::Sin(Time * WobbleFrequency * UE_TWO_PI + AxisOffset);
	const float Secondary = SecondaryAmplitude * FMath::Sin(Time * SecondaryFrequency * UE_TWO_PI + AxisOffset * 2.1f);
	return Primary + Secondary;
}

void UFPVTiltComponent::UpdateSpring(float DeltaTime)
{
	// Underdamped spring-damper system for each axis independently
	// Uses per-axis interp speeds as spring frequency
	// SpringDamping < 1.0 gives overshoot

	auto SpringAxis = [this, DeltaTime](double& Current, double& Vel, double Target, float InterpSpeed)
	{
		// Spring-damper: F = -k*(x-target) - d*v
		// k = InterpSpeed^2, d = 2*damping*InterpSpeed
		const double Omega = InterpSpeed; // natural frequency
		const double Damping = SpringDamping;

		const double Error = Current - Target;
		const double SpringForce = -Omega * Omega * Error;
		const double DampingForce = -2.0 * Damping * Omega * Vel;

		Vel += (SpringForce + DampingForce) * DeltaTime;
		Current += Vel * DeltaTime;
	};

	SpringAxis(CurrentAngles.Pitch, AngularVelocity.Pitch, TargetAngles.Pitch, PitchInterpSpeed);
	SpringAxis(CurrentAngles.Roll, AngularVelocity.Roll, TargetAngles.Roll, RollInterpSpeed);
	SpringAxis(CurrentAngles.Yaw, AngularVelocity.Yaw, TargetAngles.Yaw, YawInterpSpeed);
}
