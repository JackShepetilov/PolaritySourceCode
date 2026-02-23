// WeaponRecoilComponent.cpp
// Advanced procedural recoil system with spring-based visual kick and organic sway

#include "WeaponRecoilComponent.h"
#include "ApexMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"

UWeaponRecoilComponent::UWeaponRecoilComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UWeaponRecoilComponent::BeginPlay()
{
	Super::BeginPlay();

	// Initialize random stream with unique seed per component instance
	SwayRandomStream.Initialize(GetUniqueID());

	// Try to get references from owner
	if (AActor* Owner = GetOwner())
	{
		if (APawn* Pawn = Cast<APawn>(Owner))
		{
			OwnerController = Cast<APlayerController>(Pawn->GetController());

			if (ACharacter* Character = Cast<ACharacter>(Pawn))
			{
				MovementComponent = Character->GetCharacterMovement();
			}
		}
	}
}

void UWeaponRecoilComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Update time since last shot
	TimeSinceLastShot += DeltaTime;

	// Reset consecutive multiplier if not firing for a while
	if (!bIsFiring && TimeSinceLastShot > 0.3f)
	{
		CurrentConsecutiveMultiplier = FMath::FInterpTo(CurrentConsecutiveMultiplier, 1.0f, DeltaTime, 5.0f);
		CurrentShotIndex = 0;
	}

	// Update all systems
	UpdateCameraRecoilSpring(DeltaTime); // Must be before Recovery (feeds AccumulatedRecoil)
	UpdateRecovery(DeltaTime);
	UpdateVisualKick(DeltaTime);
	UpdateCameraPunch(DeltaTime);
	UpdateWeaponSway(DeltaTime);
}

// ==================== Setup ====================

void UWeaponRecoilComponent::Initialize(APlayerController* InController, UCharacterMovementComponent* InMovement, UApexMovementComponent* InApexMovement)
{
	OwnerController = InController;
	MovementComponent = InMovement;
	ApexMovement = InApexMovement;
}

void UWeaponRecoilComponent::SetRecoilSettings(const FWeaponRecoilSettings& InSettings)
{
	Settings = InSettings;
}

// ==================== Firing Events ====================

void UWeaponRecoilComponent::OnWeaponFired()
{
	bIsFiring = true;
	TimeSinceLastShot = 0.0f;
	bIsRecovering = false;

	// Calculate total recoil for this shot
	FRotator TotalRecoil = CalculateShotRecoil();

	// Split recoil between camera and viewmodel based on ADS state (Titanfall 2-style)
	float Fraction = bIsAiming ? Settings.ADSWeaponFraction : Settings.HipfireWeaponFraction;
	float VMScale = bIsAiming ? Settings.ADSVMScale : Settings.HipfireVMScale;

	FRotator CameraRecoil = TotalRecoil * (1.0f - Fraction);
	FRotator ViewmodelRecoil = TotalRecoil * Fraction * VMScale;

	// Apply camera portion to controller (queued into spring for smooth delivery)
	ApplyRecoilToController(CameraRecoil);

	// NOTE: AccumulatedRecoil is now tracked per-frame in UpdateCameraRecoilSpring()
	// instead of being accumulated instantly here

	// Generate independent roll (Titanfall 2-style: random direction per shot, not part of weaponFraction)
	float RollMagnitude = FMath::RandRange(Settings.RollRandomMin, Settings.RollRandomMax);
	float RollSign = FMath::RandBool() ? 1.0f : -1.0f;
	float ShotRoll = RollSign * RollMagnitude * Settings.RollHardScale;

	// Trigger visual effects
	if (Settings.bEnableVisualKick)
	{
		TriggerVisualKick(ViewmodelRecoil, ShotRoll);
	}

	if (Settings.bEnableCameraPunch)
	{
		TriggerCameraPunch();
	}

	// Update consecutive shot state
	CurrentShotIndex++;
	CurrentConsecutiveMultiplier = FMath::Min(
		CurrentConsecutiveMultiplier * Settings.ConsecutiveShotMultiplier,
		Settings.MaxConsecutiveMultiplier
	);

	UE_LOG(LogTemp, Verbose, TEXT("Recoil: Shot %d, Mult=%.2f, Total=(P:%.2f, Y:%.2f), Camera=(P:%.2f, Y:%.2f), VM=(P:%.2f, Y:%.2f)"),
		CurrentShotIndex, CurrentConsecutiveMultiplier,
		TotalRecoil.Pitch, TotalRecoil.Yaw,
		CameraRecoil.Pitch, CameraRecoil.Yaw,
		ViewmodelRecoil.Pitch, ViewmodelRecoil.Yaw);
}

void UWeaponRecoilComponent::OnFiringEnded()
{
	bIsFiring = false;
}

void UWeaponRecoilComponent::ResetRecoil()
{
	CurrentShotIndex = 0;
	CurrentConsecutiveMultiplier = 1.0f;
	AccumulatedRecoil = FRotator::ZeroRotator;
	bIsRecovering = false;
	bIsFiring = false;

	CurrentWeaponOffset = FVector::ZeroVector;
	CurrentWeaponRotation = FRotator::ZeroRotator;

	KickSpringPitch.Reset();
	KickSpringYaw.Reset();
	KickSpringRoll.Reset();
	KickSpringBack.Reset();

	CameraRecoilSpringPitch.Reset();
	CameraRecoilSpringYaw.Reset();

	CurrentCameraPunch = FRotator::ZeroRotator;
	CurrentSwayOffset = FRotator::ZeroRotator;

	FMemory::Memzero(BreathingOU, sizeof(BreathingOU));
	FMemory::Memzero(TremorOU, sizeof(TremorOU));
	FMemory::Memzero(JitterOU, sizeof(JitterOU));

	SwayOverrideMultiplier = 1.0f;
}

// ==================== Input ====================

void UWeaponRecoilComponent::AddMouseInput(float DeltaYaw, float DeltaPitch)
{
	CurrentMouseVelocity.X = DeltaYaw;
	CurrentMouseVelocity.Y = DeltaPitch;

	// If player is manually pulling down during recovery, reduce accumulated recoil
	if (Settings.bAllowManualRecovery && bIsRecovering && DeltaPitch < 0.0f)
	{
		// Player is pulling down - reduce recovery amount
		float ManualRecovery = FMath::Abs(DeltaPitch) * 0.8f;
		AccumulatedRecoil.Pitch = FMath::Max(0.0f, AccumulatedRecoil.Pitch - ManualRecovery);
	}
}

// ==================== Internal Methods ====================

FRotator UWeaponRecoilComponent::CalculateShotRecoil()
{
	FRotator Recoil = FRotator::ZeroRotator;

	// Get base recoil from pattern or random
	if (Settings.RecoilPattern.Num() > 0)
	{
		// Smart pattern looping: play full pattern once, then loop from mid-point
		// This prevents repeating the aggressive opening climb
		int32 PatternIndex;
		if (CurrentShotIndex < Settings.RecoilPattern.Num())
		{
			PatternIndex = CurrentShotIndex;
		}
		else
		{
			int32 LoopStart;
			if (Settings.PatternLoopStartIndex >= 0)
			{
				LoopStart = FMath::Clamp(Settings.PatternLoopStartIndex, 0, Settings.RecoilPattern.Num() - 1);
			}
			else
			{
				// Auto mid-point
				LoopStart = Settings.RecoilPattern.Num() / 2;
			}
			int32 LoopLength = Settings.RecoilPattern.Num() - LoopStart;
			PatternIndex = LoopStart + ((CurrentShotIndex - Settings.RecoilPattern.Num()) % LoopLength);
		}
		const FRecoilPatternPoint& PatternPoint = Settings.RecoilPattern[PatternIndex];

		Recoil.Pitch = PatternPoint.Pitch;
		Recoil.Yaw = PatternPoint.Yaw;

		// Add randomness based on PatternRandomness
		float RandomPitch = FMath::RandRange(-Settings.BaseVerticalRecoil, Settings.BaseVerticalRecoil) * 0.3f;
		float RandomYaw = FMath::RandRange(-Settings.BaseHorizontalRecoil, Settings.BaseHorizontalRecoil);

		Recoil.Pitch += RandomPitch * Settings.PatternRandomness;
		Recoil.Yaw += RandomYaw * Settings.PatternRandomness;
	}
	else
	{
		// No pattern - use base values with full randomness
		Recoil.Pitch = Settings.BaseVerticalRecoil + FMath::RandRange(0.0f, Settings.BaseVerticalRecoil * 0.5f);
		Recoil.Yaw = FMath::RandRange(-Settings.BaseHorizontalRecoil, Settings.BaseHorizontalRecoil);
	}

	// Apply consecutive shot multiplier
	Recoil.Pitch *= CurrentConsecutiveMultiplier;
	Recoil.Yaw *= CurrentConsecutiveMultiplier;

	// Apply situational multiplier (airborne, crouching, ADS, moving)
	float SituationalMult = GetSituationalMultiplier();
	Recoil.Pitch *= SituationalMult;
	Recoil.Yaw *= SituationalMult;

	return Recoil;
}

float UWeaponRecoilComponent::GetSituationalMultiplier() const
{
	float Multiplier = 1.0f;

	// Airborne increases recoil
	if (IsAirborne())
	{
		Multiplier *= Settings.AirborneRecoilMultiplier;
	}

	// Crouching reduces recoil
	if (bIsCrouching)
	{
		Multiplier *= Settings.CrouchRecoilMultiplier;
	}

	// ADS reduces recoil
	if (bIsAiming)
	{
		Multiplier *= Settings.ADSRecoilMultiplier;
	}

	// Moving increases recoil slightly
	if (IsMoving() && !IsAirborne())
	{
		Multiplier *= Settings.MovingRecoilMultiplier;
	}

	return Multiplier;
}

bool UWeaponRecoilComponent::IsAirborne() const
{
	// Prefer ApexMovement if available
	if (ApexMovement)
	{
		return ApexMovement->IsFalling() || ApexMovement->IsWallRunning();
	}

	if (!MovementComponent) return false;
	return MovementComponent->IsFalling();
}

bool UWeaponRecoilComponent::IsMoving() const
{
	if (ApexMovement)
	{
		return ApexMovement->IsMovingOnGround() || ApexMovement->GetSpeedRatio() > 0.1f;
	}

	if (!MovementComponent) return false;
	return MovementComponent->Velocity.Size2D() > 50.0f;
}

void UWeaponRecoilComponent::ApplyRecoilToController(const FRotator& Recoil)
{
	// Instead of instant AddPitchInput/AddYawInput, queue velocity impulse into springs.
	// The springs will smoothly deliver the recoil over 2-3 frames.
	CameraRecoilSpringPitch.Velocity += Recoil.Pitch * 30.0f;
	CameraRecoilSpringYaw.Velocity += Recoil.Yaw * 30.0f;
}

// ==================== Camera Recoil Spring ====================

void UWeaponRecoilComponent::UpdateCameraRecoilSpring(float DeltaTime)
{
	if (!OwnerController) return;

	// Capture previous spring positions
	float PrevPitch = CameraRecoilSpringPitch.Value;
	float PrevYaw = CameraRecoilSpringYaw.Value;

	// Sub-step the spring to prevent explosion on large DeltaTime (hitches, first frame, etc.)
	// Euler integration of a stiff spring is unstable when dt is too large,
	// causing massive single-frame camera kicks.
	constexpr float MaxSubStep = 1.0f / 60.0f; // ~16.6ms max per step
	float Remaining = DeltaTime;
	while (Remaining > KINDA_SMALL_NUMBER)
	{
		float Step = FMath::Min(Remaining, MaxSubStep);
		CameraRecoilSpringPitch.Update(0.0f, Settings.CameraRecoilSpringStiffness, Step);
		CameraRecoilSpringYaw.Update(0.0f, Settings.CameraRecoilSpringStiffness, Step);
		Remaining -= Step;
	}

	// Calculate delta this frame (how much the spring moved)
	float DeltaPitch = CameraRecoilSpringPitch.Value - PrevPitch;
	float DeltaYaw = CameraRecoilSpringYaw.Value - PrevYaw;

	// Apply smoothed delta to controller
	// Negative pitch = look up (recoil goes up)
	if (FMath::Abs(DeltaPitch) > KINDA_SMALL_NUMBER)
	{
		OwnerController->AddPitchInput(-DeltaPitch);
	}
	if (FMath::Abs(DeltaYaw) > KINDA_SMALL_NUMBER)
	{
		OwnerController->AddYawInput(DeltaYaw);
	}

	// Track accumulated recoil for recovery system
	AccumulatedRecoil.Pitch += FMath::Abs(DeltaPitch);
	AccumulatedRecoil.Yaw += DeltaYaw;
}

// ==================== Recovery ====================

void UWeaponRecoilComponent::UpdateRecovery(float DeltaTime)
{
	// Don't recover while actively firing
	if (bIsFiring) return;

	// Wait for recovery delay
	if (TimeSinceLastShot < Settings.RecoveryDelay) return;

	// Don't recover while camera springs are still delivering recoil
	if (FMath::Abs(CameraRecoilSpringPitch.Velocity) > 1.0f ||
		FMath::Abs(CameraRecoilSpringYaw.Velocity) > 1.0f)
	{
		return;
	}

	// No recovery if nothing accumulated
	if (AccumulatedRecoil.IsNearlyZero(0.01f))
	{
		bIsRecovering = false;
		return;
	}

	bIsRecovering = true;

	// Calculate recovery amount this frame
	float RecoveryAmount = Settings.RecoverySpeed * DeltaTime;

	// Recover pitch (bring camera back down)
	if (AccumulatedRecoil.Pitch > 0.01f)
	{
		float PitchRecovery = FMath::Min(RecoveryAmount, AccumulatedRecoil.Pitch);
		AccumulatedRecoil.Pitch -= PitchRecovery;

		// Apply recovery to controller (positive pitch = look down)
		if (OwnerController)
		{
			OwnerController->AddPitchInput(PitchRecovery);
		}
	}

	// Recover yaw (center horizontal)
	if (FMath::Abs(AccumulatedRecoil.Yaw) > 0.01f)
	{
		float YawRecovery = FMath::Min(RecoveryAmount * 0.5f, FMath::Abs(AccumulatedRecoil.Yaw));
		float YawSign = FMath::Sign(AccumulatedRecoil.Yaw);
		AccumulatedRecoil.Yaw -= YawRecovery * YawSign;

		if (OwnerController)
		{
			OwnerController->AddYawInput(-YawRecovery * YawSign);
		}
	}
}

// ==================== Visual Kick (Spring-Damper) ====================

void UWeaponRecoilComponent::TriggerVisualKick(const FRotator& ViewmodelRecoil, float RollKick)
{
	// Apply as velocity impulses to springs (not target positions)
	// This preserves momentum from previous kicks, creating smooth continuous motion
	KickSpringPitch.Velocity += ViewmodelRecoil.Pitch * 30.0f;
	KickSpringYaw.Velocity += ViewmodelRecoil.Yaw * 30.0f;
	KickSpringRoll.Velocity += RollKick * 30.0f;
	KickSpringBack.Velocity += -Settings.KickBackDistance * 30.0f;
}

void UWeaponRecoilComponent::UpdateVisualKick(float DeltaTime)
{
	if (!Settings.bEnableVisualKick) return;

	// Sub-step visual kick springs to prevent instability on large DeltaTime
	constexpr float MaxSubStep = 1.0f / 60.0f;
	float Remaining = DeltaTime;
	while (Remaining > KINDA_SMALL_NUMBER)
	{
		float Step = FMath::Min(Remaining, MaxSubStep);
		KickSpringPitch.Update(0.0f, Settings.KickSpringStiffness, Step);
		KickSpringYaw.Update(0.0f, Settings.KickSpringStiffness, Step);
		KickSpringRoll.Update(0.0f, Settings.KickSpringStiffness, Step);
		KickSpringBack.Update(0.0f, Settings.KickSpringStiffness, Step);
		Remaining -= Step;
	}

	// Read spring values into current weapon transform
	CurrentWeaponRotation.Pitch = KickSpringPitch.Value;
	CurrentWeaponRotation.Yaw = KickSpringYaw.Value;
	CurrentWeaponRotation.Roll = KickSpringRoll.Value;
	CurrentWeaponOffset.X = KickSpringBack.Value;
}

// ==================== Camera Punch ====================

void UWeaponRecoilComponent::TriggerCameraPunch()
{
	// Random punch direction with bias upward
	PunchOscillationAmplitude.X = FMath::RandRange(-1.0f, 1.0f) * Settings.CameraPunchIntensity;
	PunchOscillationAmplitude.Y = FMath::RandRange(0.0f, 1.0f) * Settings.CameraPunchIntensity;

	PunchOscillationTime = 0.0f;
}

void UWeaponRecoilComponent::UpdateCameraPunch(float DeltaTime)
{
	if (!Settings.bEnableCameraPunch) return;

	// Damped oscillation for camera punch
	if (FMath::Abs(PunchOscillationAmplitude.X) > 0.001f || FMath::Abs(PunchOscillationAmplitude.Y) > 0.001f)
	{
		PunchOscillationTime += DeltaTime;

		float Decay = FMath::Exp(-Settings.CameraPunchDamping * PunchOscillationTime);
		float Phase = PunchOscillationTime * Settings.CameraPunchFrequency * 2.0f * PI;
		float SinValue = FMath::Sin(Phase) * Decay;

		CurrentCameraPunch.Yaw = PunchOscillationAmplitude.X * SinValue;
		CurrentCameraPunch.Pitch = PunchOscillationAmplitude.Y * SinValue;
		CurrentCameraPunch.Roll = PunchOscillationAmplitude.X * SinValue * 0.3f;

		// Reset when negligible
		if (Decay < 0.01f)
		{
			PunchOscillationAmplitude = FVector2D::ZeroVector;
			CurrentCameraPunch = FRotator::ZeroRotator;
		}
	}
}

// ==================== Weapon Sway ====================

void UWeaponRecoilComponent::UpdateWeaponSway(float DeltaTime)
{
	if (!Settings.bEnableWeaponSway) return;

	// Smooth mouse velocity
	SmoothedMouseVelocity = FMath::Vector2DInterpTo(
		SmoothedMouseVelocity,
		CurrentMouseVelocity,
		DeltaTime,
		Settings.MouseSwayLag
	);

	// Reset current mouse velocity (it gets set each frame from input)
	CurrentMouseVelocity = FVector2D::ZeroVector;

	// Calculate mouse sway
	FRotator MouseSway = FRotator::ZeroRotator;
	MouseSway.Yaw = FMath::Clamp(
		-SmoothedMouseVelocity.X * Settings.MouseSwayIntensity,
		-Settings.MaxMouseSwayOffset,
		Settings.MaxMouseSwayOffset
	);
	MouseSway.Pitch = FMath::Clamp(
		SmoothedMouseVelocity.Y * Settings.MouseSwayIntensity,
		-Settings.MaxMouseSwayOffset,
		Settings.MaxMouseSwayOffset
	);

	// Ornstein-Uhlenbeck organic sway (truly stochastic, never repeats)
	FRotator OrganicSway = FRotator::ZeroRotator;

	if (Settings.bEnableOrganicSway)
	{
		// Layer 1: Breathing (slow, large drift)
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			BreathingOU[Axis] = AdvanceOU(BreathingOU[Axis],
				Settings.BreathingReversionSpeed, Settings.BreathingVolatility,
				Settings.BreathingMaxAngle, DeltaTime);
		}
		OrganicSway.Pitch += BreathingOU[0] * Settings.BreathingAxisScale.X;
		OrganicSway.Yaw   += BreathingOU[1] * Settings.BreathingAxisScale.Y;
		OrganicSway.Roll  += BreathingOU[2] * Settings.BreathingAxisScale.Z;

		// Layer 2: Tremor (medium speed, hand instability)
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			TremorOU[Axis] = AdvanceOU(TremorOU[Axis],
				Settings.TremorReversionSpeed, Settings.TremorVolatility,
				Settings.TremorMaxAngle, DeltaTime);
		}
		OrganicSway.Pitch += TremorOU[0] * Settings.TremorAxisScale.X;
		OrganicSway.Yaw   += TremorOU[1] * Settings.TremorAxisScale.Y;
		OrganicSway.Roll  += TremorOU[2] * Settings.TremorAxisScale.Z;

		// Layer 3: Micro-jitter (fast, nervous system noise)
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			JitterOU[Axis] = AdvanceOU(JitterOU[Axis],
				Settings.JitterReversionSpeed, Settings.JitterVolatility,
				Settings.JitterMaxAngle, DeltaTime);
		}
		OrganicSway.Pitch += JitterOU[0] * Settings.JitterAxisScale.X;
		OrganicSway.Yaw   += JitterOU[1] * Settings.JitterAxisScale.Y;
		OrganicSway.Roll  += JitterOU[2] * Settings.JitterAxisScale.Z;
	}

	// Movement sway multiplier
	float MovementMult = 1.0f;
	if (IsMoving())
	{
		MovementMult = Settings.MovementSwayMultiplier;
	}

	// Reduce sway when aiming
	float AimMult = bIsAiming ? Settings.ADSSwayMultiplier : 1.0f;

	// Combine all sway sources
	FRotator TotalSway = (MouseSway + OrganicSway * MovementMult) * AimMult * SwayOverrideMultiplier;

	// Smooth interpolation to target sway
	CurrentSwayOffset = FMath::RInterpTo(CurrentSwayOffset, TotalSway, DeltaTime, Settings.MouseSwayLag);

	// Add sway to weapon rotation
	CurrentWeaponRotation += CurrentSwayOffset;
}

// ==================== Ornstein-Uhlenbeck Process ====================

float UWeaponRecoilComponent::AdvanceOU(float CurrentValue, float ReversionSpeed, float Volatility, float MaxAngle, float DeltaTime)
{
	// Ornstein-Uhlenbeck: dX = θ(μ - X)dt + σ * dW
	// μ = 0 (center), θ = ReversionSpeed, σ = Volatility
	// Approximate Gaussian noise via Central Limit Theorem (sum of 3 uniforms)
	float U1 = SwayRandomStream.FRandRange(-1.0f, 1.0f);
	float U2 = SwayRandomStream.FRandRange(-1.0f, 1.0f);
	float U3 = SwayRandomStream.FRandRange(-1.0f, 1.0f);
	float GaussianApprox = (U1 + U2 + U3) / 1.732f; // ~N(0,1)

	float Drift = ReversionSpeed * (0.0f - CurrentValue) * DeltaTime;
	float Diffusion = Volatility * GaussianApprox * FMath::Sqrt(DeltaTime);

	float NewValue = CurrentValue + Drift + Diffusion;
	return FMath::Clamp(NewValue, -MaxAngle, MaxAngle);
}

// ==================== Default Pattern ====================

TArray<FRecoilPatternPoint> UWeaponRecoilComponent::GetDefaultAssaultRiflePattern()
{
	TArray<FRecoilPatternPoint> Pattern;
	Pattern.Reserve(20);

	// R-201 style: moderate opening climb, then horizontal meander
	// Phase 1: Shots 1-5 — moderate upward climb, slight rightward drift
	Pattern.Add(FRecoilPatternPoint(0.30f,  0.05f));   // Shot 1
	Pattern.Add(FRecoilPatternPoint(0.35f,  0.08f));   // Shot 2
	Pattern.Add(FRecoilPatternPoint(0.30f,  0.12f));   // Shot 3
	Pattern.Add(FRecoilPatternPoint(0.25f,  0.10f));   // Shot 4
	Pattern.Add(FRecoilPatternPoint(0.28f, -0.05f));   // Shot 5

	// Phase 2: Shots 6-10 — reduced vertical, alternating horizontal
	Pattern.Add(FRecoilPatternPoint(0.18f, -0.15f));   // Shot 6
	Pattern.Add(FRecoilPatternPoint(0.15f, -0.20f));   // Shot 7
	Pattern.Add(FRecoilPatternPoint(0.20f,  0.10f));   // Shot 8
	Pattern.Add(FRecoilPatternPoint(0.12f,  0.22f));   // Shot 9
	Pattern.Add(FRecoilPatternPoint(0.15f,  0.18f));   // Shot 10

	// Phase 3: Shots 11-15 — minimal vertical, wider horizontal oscillation
	Pattern.Add(FRecoilPatternPoint(0.10f, -0.25f));   // Shot 11
	Pattern.Add(FRecoilPatternPoint(0.08f, -0.30f));   // Shot 12
	Pattern.Add(FRecoilPatternPoint(0.12f, -0.10f));   // Shot 13
	Pattern.Add(FRecoilPatternPoint(0.10f,  0.20f));   // Shot 14
	Pattern.Add(FRecoilPatternPoint(0.08f,  0.35f));   // Shot 15

	// Phase 4: Shots 16-20 — near-zero vertical, horizontal meander
	Pattern.Add(FRecoilPatternPoint(0.05f,  0.25f));   // Shot 16
	Pattern.Add(FRecoilPatternPoint(0.10f, -0.15f));   // Shot 17
	Pattern.Add(FRecoilPatternPoint(0.08f, -0.28f));   // Shot 18
	Pattern.Add(FRecoilPatternPoint(0.05f,  0.10f));   // Shot 19
	Pattern.Add(FRecoilPatternPoint(0.10f,  0.18f));   // Shot 20

	return Pattern;
}
