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

	// Apply camera portion to controller (this moves the crosshair)
	ApplyRecoilToController(CameraRecoil);

	// Accumulate only camera portion for recovery
	AccumulatedRecoil += CameraRecoil;

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

	CurrentCameraPunch = FRotator::ZeroRotator;
	CurrentSwayOffset = FRotator::ZeroRotator;
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
		// Use pattern (loop if we exceed pattern length)
		int32 PatternIndex = CurrentShotIndex % Settings.RecoilPattern.Num();
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
	if (!OwnerController) return;

	// Apply as control rotation change
	// Negative pitch = look up (recoil goes up)
	OwnerController->AddPitchInput(-Recoil.Pitch);
	OwnerController->AddYawInput(Recoil.Yaw);
}

// ==================== Recovery ====================

void UWeaponRecoilComponent::UpdateRecovery(float DeltaTime)
{
	// Don't recover while actively firing
	if (bIsFiring) return;

	// Wait for recovery delay
	if (TimeSinceLastShot < Settings.RecoveryDelay) return;

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

	// Update all springs toward rest position (0)
	// Springs naturally handle momentum, overshoot, and smooth recovery
	KickSpringPitch.Update(0.0f, Settings.KickSpringStiffness, DeltaTime);
	KickSpringYaw.Update(0.0f, Settings.KickSpringStiffness, DeltaTime);
	KickSpringRoll.Update(0.0f, Settings.KickSpringStiffness, DeltaTime);
	KickSpringBack.Update(0.0f, Settings.KickSpringStiffness, DeltaTime);

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

	// Multi-layered organic breathing sway (irrational frequencies = never repeats visually)
	BreathingTime += DeltaTime;
	float T = BreathingTime;

	FRotator BreathingSway = FRotator::ZeroRotator;

	// Layer 1: Slow breathing rhythm (0.2-0.3 Hz) - base heaving
	BreathingSway.Pitch = Settings.BreathingAmplitude * FMath::Sin(T * 1.37f);
	BreathingSway.Yaw   = Settings.BreathingAmplitude * 0.6f * FMath::Sin(T * 0.93f + 0.7f);
	BreathingSway.Roll  = Settings.BreathingAmplitude * 0.3f * FMath::Sin(T * 0.71f + 1.3f);

	// Layer 2: Muscle tremor (1-3 Hz) - hand instability
	BreathingSway.Pitch += Settings.TremorAmplitude * FMath::Sin(T * 8.73f);
	BreathingSway.Yaw   += Settings.TremorAmplitude * 0.7f * FMath::Sin(T * 6.41f + 2.1f);
	BreathingSway.Roll  += Settings.TremorAmplitude * 0.5f * FMath::Sin(T * 11.17f + 0.9f);

	// Layer 3: Micro-jitter (5-8 Hz) - nervous system noise
	BreathingSway.Pitch += Settings.MicroJitterAmplitude * FMath::Sin(T * 29.3f);
	BreathingSway.Yaw   += Settings.MicroJitterAmplitude * FMath::Sin(T * 37.1f + 1.7f);

	// Movement sway multiplier
	float MovementMult = 1.0f;
	if (IsMoving())
	{
		MovementMult = Settings.MovementSwayMultiplier;
	}

	// Reduce sway when aiming
	float AimMult = bIsAiming ? 0.3f : 1.0f;

	// Combine all sway sources
	FRotator TotalSway = (MouseSway + BreathingSway * MovementMult) * AimMult;

	// Smooth interpolation to target sway
	CurrentSwayOffset = FMath::RInterpTo(CurrentSwayOffset, TotalSway, DeltaTime, Settings.MouseSwayLag);

	// Add sway to weapon rotation
	CurrentWeaponRotation += CurrentSwayOffset;
}
