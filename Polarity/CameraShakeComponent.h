// CameraShakeComponent.h
// Procedural camera shake system with damped harmonic oscillator physics
// Includes Titanfall 2/Apex Legends style procedural walk/sprint bob

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CameraShakeComponent.generated.h"

class UMovementSettings;
class UApexMovementComponent;
class UCameraComponent;
class APlayerController;

// ==================== Enums ====================

/**
 * Camera bob intensity presets (matches Apex Legends "Sprint View Shake" setting)
 */
UENUM(BlueprintType)
enum class ECameraBobIntensity : uint8
{
	/** Almost no camera bob - viewmodel only */
	Minimal,
	/** Light camera bob */
	Low,
	/** Default camera bob */
	Medium,
	/** Strong camera bob */
	High
};

// ==================== Support Structs ====================

/**
 * Damped harmonic oscillator for physically-based shake decay
 * Formula: x(t) = Amplitude * e^(-Damping * t) * sin(2π * Frequency * t)
 */
USTRUCT(BlueprintType)
struct FDampedOscillator
{
	GENERATED_BODY()

	UPROPERTY()
	bool bActive = false;

	UPROPERTY()
	float Time = 0.0f;

	UPROPERTY()
	float Amplitude = 0.0f;

	UPROPERTY()
	float Frequency = 15.0f;

	UPROPERTY()
	float Damping = 5.0f;
};

/**
 * Spring state for smooth interpolation (critically-damped)
 */
USTRUCT(BlueprintType)
struct FBobSpringState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Bob")
	float Value = 0.0f;

	UPROPERTY()
	float Velocity = 0.0f;

	void Update(float Target, float Stiffness, float DeltaTime)
	{
		const float Damping = 2.0f * FMath::Sqrt(Stiffness);
		const float Error = Target - Value;
		const float Acceleration = Stiffness * Error - Damping * Velocity;

		Velocity += Acceleration * DeltaTime;
		Value += Velocity * DeltaTime;
	}

	void Reset(float NewValue = 0.0f)
	{
		Value = NewValue;
		Velocity = 0.0f;
	}
};

/**
 * Lissajous (figure-8) bob generator state
 * X = sin(ωt), Y = sin(2ωt) - mimics bipedal walking
 */
USTRUCT(BlueprintType)
struct FLissajousBobState
{
	GENERATED_BODY()

	UPROPERTY()
	float Phase = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Bob")
	float HorizontalOffset = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Bob")
	float VerticalOffset = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Bob")
	float RollOffset = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Bob")
	float PitchOffset = 0.0f;

	void Update(float DeltaPhase, float Intensity, float HorizAmp, float VertAmp, float RollAmp, float PitchAmp)
	{
		Phase += DeltaPhase;
		if (Phase > 2.0f * PI) Phase -= 2.0f * PI;

		const float SinPhase = FMath::Sin(Phase);
		const float Sin2Phase = FMath::Sin(2.0f * Phase);
		const float Cos2Phase = FMath::Cos(2.0f * Phase);

		HorizontalOffset = SinPhase * HorizAmp * Intensity;
		VerticalOffset = Sin2Phase * VertAmp * Intensity;
		RollOffset = SinPhase * RollAmp * Intensity;
		PitchOffset = Cos2Phase * PitchAmp * Intensity;
	}

	void Reset()
	{
		Phase = 0.0f;
		HorizontalOffset = 0.0f;
		VerticalOffset = 0.0f;
		RollOffset = 0.0f;
		PitchOffset = 0.0f;
	}
};

/**
 * Complete procedural bob configuration and state
 * Titanfall 2 / Apex Legends style walk/sprint bob
 */
USTRUCT(BlueprintType)
struct FProceduralBobState
{
	GENERATED_BODY()

	// ==================== Enable Flags ====================

	/** Master enable for procedural bob */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|General")
	bool bEnabled = true;

	/** Enable viewmodel bob (separate from camera) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Viewmodel")
	bool bEnableViewmodelBob = true;

	/** Bob intensity preset (like Apex "Sprint View Shake") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|General")
	ECameraBobIntensity BobIntensity = ECameraBobIntensity::Medium;

	/** Global intensity multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|General")
	float GlobalIntensity = 1.0f;

	// ==================== Walk Settings ====================

	/** Horizontal amplitude during walking (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Walk")
	float WalkHorizontalAmplitude = 0.8f;

	/** Vertical amplitude during walking (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Walk")
	float WalkVerticalAmplitude = 1.2f;

	/** Roll amplitude during walking (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Walk")
	float WalkRollAmplitude = 0.4f;

	/** Pitch amplitude during walking (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Walk")
	float WalkPitchAmplitude = 0.15f;

	/** Distance per step during walking (cm) - affects frequency */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Walk")
	float WalkStepDistance = 70.0f;

	// ==================== Sprint Settings ====================

	/** Horizontal amplitude multiplier during sprinting */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Sprint")
	float SprintHorizontalMultiplier = 1.4f;

	/** Vertical amplitude multiplier during sprinting */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Sprint")
	float SprintVerticalMultiplier = 1.6f;

	/** Roll amplitude multiplier during sprinting */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Sprint")
	float SprintRollMultiplier = 1.5f;

	/** Pitch amplitude multiplier during sprinting */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Sprint")
	float SprintPitchMultiplier = 1.3f;

	/** Distance per step during sprinting (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Sprint")
	float SprintStepDistance = 110.0f;

	// ==================== Viewmodel Settings ====================

	/** Multiplier for viewmodel bob relative to camera */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Viewmodel", meta = (EditCondition = "bEnableViewmodelBob"))
	float ViewmodelBobMultiplier = 2.5f;

	/** Forward/back sway for figure-8 depth (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Viewmodel", meta = (EditCondition = "bEnableViewmodelBob"))
	float ViewmodelForwardSway = 0.5f;

	// ==================== Spring Settings ====================

	/** Spring stiffness for intensity (higher = faster response) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Spring")
	float IntensitySpringStiffness = 80.0f;

	/** Spring stiffness for walk/sprint transitions */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Spring")
	float StateSpringStiffness = 40.0f;

	// ==================== Thresholds ====================

	/** Minimum speed to start bob (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Thresholds")
	float MinSpeedForBob = 100.0f;

	/** Speed at which bob reaches full intensity (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bob|Thresholds")
	float FullIntensitySpeed = 400.0f;

	// ==================== Runtime State ====================

	FLissajousBobState BobGenerator;
	FBobSpringState IntensitySpring;
	FBobSpringState SprintBlendSpring;

	// ==================== Helper Methods ====================

	float GetPresetMultiplier() const
	{
		switch (BobIntensity)
		{
		case ECameraBobIntensity::Minimal: return 0.3f;
		case ECameraBobIntensity::Low: return 0.6f;
		case ECameraBobIntensity::Medium: return 1.0f;
		case ECameraBobIntensity::High: return 1.4f;
		default: return 1.0f;
		}
	}

	float GetCameraScale() const
	{
		switch (BobIntensity)
		{
		case ECameraBobIntensity::Minimal: return 0.15f;
		case ECameraBobIntensity::Low: return 0.4f;
		case ECameraBobIntensity::Medium: return 0.7f;
		case ECameraBobIntensity::High: return 1.0f;
		default: return 0.7f;
		}
	}
};

// ==================== Main Component ====================

/**
 * Procedural camera shake component using damped harmonic oscillators.
 * Provides physically accurate shake decay for impacts and continuous
 * effects for movement (wallrun, slide).
 *
 * Walk/Sprint bob uses Titanfall 2/Apex Legends style Lissajous (figure-8) pattern.
 */
UCLASS(ClassGroup = (Camera), meta = (BlueprintSpawnableComponent))
class POLARITY_API UCameraShakeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCameraShakeComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==================== Setup ====================

	UFUNCTION(BlueprintCallable, Category = "Camera Shake")
	void Initialize(UCameraComponent* InCamera, UApexMovementComponent* InMovement, UMovementSettings* InSettings);

	// ==================== Event Triggers ====================

	/** Trigger landing shake based on fall velocity */
	UFUNCTION(BlueprintCallable, Category = "Camera Shake")
	void TriggerLandingShake(float FallVelocity);

	/** Trigger jump camera kick */
	UFUNCTION(BlueprintCallable, Category = "Camera Shake")
	void TriggerJumpShake(bool bIsDoubleJump);

	/** Start slide shake effect */
	UFUNCTION(BlueprintCallable, Category = "Camera Shake")
	void TriggerSlideStart();

	/** End slide shake effect */
	UFUNCTION(BlueprintCallable, Category = "Camera Shake")
	void TriggerSlideEnd();

	/** Trigger air dash FOV effect */
	UFUNCTION(BlueprintCallable, Category = "Camera Shake")
	void TriggerAirDash();

	/** Start wallrun bob effect */
	UFUNCTION(BlueprintCallable, Category = "Camera Shake")
	void TriggerWallrunStart();

	/** End wallrun bob effect */
	UFUNCTION(BlueprintCallable, Category = "Camera Shake")
	void TriggerWallrunEnd();

	// ==================== Output Getters ====================

	/** Get total camera position offset (bob + shake) */
	UFUNCTION(BlueprintPure, Category = "Camera Shake")
	FVector GetCameraOffset() const { return CurrentOffset; }

	/** Get total camera rotation offset (bob + shake) */
	UFUNCTION(BlueprintPure, Category = "Camera Shake")
	FRotator GetCameraRotationOffset() const { return CurrentRotationOffset; }

	/** Get FOV offset from effects */
	UFUNCTION(BlueprintPure, Category = "Camera Shake")
	float GetFOVOffset() const { return CurrentFOVOffset; }

	/** Get viewmodel bob position offset */
	UFUNCTION(BlueprintPure, Category = "Camera Shake|Bob")
	FVector GetViewmodelBobOffset() const { return CurrentViewmodelBobOffset; }

	/** Get viewmodel bob rotation offset */
	UFUNCTION(BlueprintPure, Category = "Camera Shake|Bob")
	FRotator GetViewmodelBobRotation() const { return CurrentViewmodelBobRotation; }

	// ==================== Procedural Bob Control ====================

	/** Get the procedural bob state for direct configuration */
	UFUNCTION(BlueprintPure, Category = "Camera Shake|Bob")
	FProceduralBobState& GetProceduralBob() { return ProceduralBob; }

	/** Set bob intensity preset (Minimal/Low/Medium/High) */
	UFUNCTION(BlueprintCallable, Category = "Camera Shake|Bob")
	void SetBobIntensity(ECameraBobIntensity NewIntensity) { ProceduralBob.BobIntensity = NewIntensity; }

	/** Get current bob intensity preset */
	UFUNCTION(BlueprintPure, Category = "Camera Shake|Bob")
	ECameraBobIntensity GetBobIntensity() const { return ProceduralBob.BobIntensity; }

	/** Enable/disable viewmodel bob */
	UFUNCTION(BlueprintCallable, Category = "Camera Shake|Bob")
	void SetViewmodelBobEnabled(bool bEnable) { ProceduralBob.bEnableViewmodelBob = bEnable; }

	/** Check if viewmodel bob is enabled */
	UFUNCTION(BlueprintPure, Category = "Camera Shake|Bob")
	bool IsViewmodelBobEnabled() const { return ProceduralBob.bEnableViewmodelBob; }

	/** Enable/disable entire procedural bob */
	UFUNCTION(BlueprintCallable, Category = "Camera Shake|Bob")
	void SetProceduralBobEnabled(bool bEnable) { ProceduralBob.bEnabled = bEnable; }

	/** Check if procedural bob is enabled */
	UFUNCTION(BlueprintPure, Category = "Camera Shake|Bob")
	bool IsProceduralBobEnabled() const { return ProceduralBob.bEnabled; }

	/** Suppress bob temporarily (e.g., during ADS) */
	UFUNCTION(BlueprintCallable, Category = "Camera Shake|Bob")
	void SetBobSuppressed(bool bSuppress) { bBobSuppressed = bSuppress; }

	/** Check if bob is suppressed */
	UFUNCTION(BlueprintPure, Category = "Camera Shake|Bob")
	bool IsBobSuppressed() const { return bBobSuppressed; }

protected:
	// ==================== References ====================

	UPROPERTY()
	TObjectPtr<UCameraComponent> CameraComponent;

	UPROPERTY()
	TObjectPtr<UApexMovementComponent> MovementComponent;

	UPROPERTY()
	TObjectPtr<UMovementSettings> Settings;

	UPROPERTY()
	TObjectPtr<APlayerController> OwnerController;

	// ==================== Current Output ====================

	FVector CurrentOffset = FVector::ZeroVector;
	FRotator CurrentRotationOffset = FRotator::ZeroRotator;
	float CurrentFOVOffset = 0.0f;

	FVector BaseCameraLocation = FVector::ZeroVector;
	float BaseFOV = 90.0f;

	// Viewmodel bob output (separate from camera)
	FVector CurrentViewmodelBobOffset = FVector::ZeroVector;
	FRotator CurrentViewmodelBobRotation = FRotator::ZeroRotator;

	// ==================== Damped Oscillators ====================

	FDampedOscillator LandingPitchOsc;
	FDampedOscillator LandingZOsc;
	FDampedOscillator JumpPitchOsc;

	// ==================== Procedural Bob ====================

	/** Procedural bob configuration and state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Shake|Bob")
	FProceduralBobState ProceduralBob;

	/** Bob suppression flag */
	bool bBobSuppressed = false;

	// ==================== Continuous Effects State ====================

	bool bIsSliding = false;
	float SlideTime = 0.0f;
	float SlideIntensity = 0.0f;

	bool bIsWallrunning = false;
	float WallrunBobTime = 0.0f;
	float WallrunFOVIntensity = 0.0f;

	float AirDashFOVTime = 0.0f;
	float AirDashFOVIntensity = 0.0f;

	// ==================== Core Methods ====================

	float UpdateDampedOscillator(FDampedOscillator& Osc, float DeltaTime);
	void TriggerOscillator(FDampedOscillator& Osc, float InAmplitude, float InFrequency, float InDamping);
	void UpdateActiveShakes(float DeltaTime);

	// ==================== Procedural Bob ====================

	void UpdateProceduralBob(float DeltaTime);

	// ==================== Continuous Effects ====================

	void UpdateSlideShake(float DeltaTime);
	void UpdateWallrunBob(float DeltaTime);
	void UpdateWallrunFOV(float DeltaTime);
	void UpdateAirDashFOV(float DeltaTime);

	void ApplyToCamera(float DeltaTime);

	// ==================== Helpers ====================

	float GetSpeedRatio() const;
	bool IsMovingOnGround() const;
	bool IsSprinting() const;
	float GetHorizontalSpeed() const;
	float PerlinNoise1D(float X) const;
};