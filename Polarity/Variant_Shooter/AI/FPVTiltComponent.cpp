// FPVTiltComponent.cpp

#include "FPVTiltComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"

UFPVTiltComponent::UFPVTiltComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // Driven by owning actor's Tick via SetMovementState
}

void UFPVTiltComponent::Initialize(UStaticMeshComponent* InMesh, float InMaxSpeed, uint32 InSeed)
{
	TargetMesh = InMesh;
	MaxSpeed = FMath::Max(InMaxSpeed, 1.0f);

	// The Blueprint-authored rotation of the mesh is the drone's real orientation (where its nose
	// is). The tilt is applied on top of it, never instead of it.
	BaseRelativeRotation = TargetMesh ? TargetMesh->GetRelativeRotation() : FRotator::ZeroRotator;

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

	// --- Calculate target angles ---

	// Pitch: the body already follows the flight path (the pawn's rotation). This adds the honest FPV
	// accent on top: nose dips under forward acceleration and rises under braking.
	const float TargetPitch = CalculateTargetPitch(Acceleration);

	// Roll: bank into turns from the sideways acceleration.
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

	// --- Apply to mesh on top of the Blueprint-authored base orientation ---
	// Composed base-first, tilt-second: the tilt is expressed in the corrected body frame (nose
	// +X), so pitch and roll angles mean what their names say regardless of the asset's own axes.
	const FQuat TiltQuat = CurrentAngles.Quaternion();
	const FQuat BaseQuat = BaseRelativeRotation.Quaternion();
	TargetMesh->SetRelativeRotation((TiltQuat * BaseQuat).Rotator());
}

float UFPVTiltComponent::CalculateTargetPitch(const FVector& Acceleration) const
{
	// Nose follows thrust: forward acceleration dips the nose, braking raises it. Signed and
	// symmetric, so backing off (the wind-up, or a punch-out against the forward vector) lifts the
	// nose exactly like a real quad. Positive UE pitch points the nose up, so acceleration takes the
	// negative.
	const FVector Forward = GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector;
	const float ForwardAccel = FVector::DotProduct(Acceleration, Forward);
	const float Alpha = FMath::Clamp(ForwardAccel / FMath::Max(PitchAccelerationReference, 1.0f), -1.0f, 1.0f);
	return -MaxPitchAngle * Alpha;
}

float UFPVTiltComponent::CalculateTargetRoll(const FVector& Velocity, const FVector& Acceleration) const
{
	if (Velocity.IsNearlyZero(1.0f))
	{
		return 0.0f;
	}

	// Lateral acceleration = cross product of velocity direction and acceleration.
	// Positive = turning right. A positive UE roll lifts the right side (that is a LEFT bank), so
	// banking into a right turn takes the negative of the lateral sign.
	const FVector VelDir = Velocity.GetSafeNormal();
	const FVector HorizAccel = FVector(Acceleration.X, Acceleration.Y, 0.0f);

	// Cross product Z component gives lateral acceleration magnitude with sign
	const float LateralAccel = FVector::CrossProduct(VelDir, HorizAccel).Z;

	return -FMath::Clamp(LateralAccel * BankMultiplier, -MaxRollAngle, MaxRollAngle);
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
