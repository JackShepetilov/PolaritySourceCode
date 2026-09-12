// CameraShakeComponent.cpp
// Procedural camera shake system with damped harmonic oscillator physics
// Now integrates ProceduralCameraBob for Titanfall 2/Apex style walk bob

#include "CameraShakeComponent.h"
#include "Camera/CameraComponent.h"
#include "ApexMovementComponent.h"
#include "MovementSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"

UCameraShakeComponent::UCameraShakeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UCameraShakeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		if (APawn* Pawn = Cast<APawn>(Owner))
		{
			OwnerController = Cast<APlayerController>(Pawn->GetController());
		}
	}
}

void UCameraShakeComponent::Initialize(UCameraComponent* InCamera, UApexMovementComponent* InMovement, UMovementSettings* InSettings)
{
	CameraComponent = InCamera;
	MovementComponent = InMovement;
	Settings = InSettings;

	if (CameraComponent)
	{
		BaseCameraLocation = CameraComponent->GetRelativeLocation();
		BaseFOV = CameraComponent->FieldOfView;
	}

	if (!Settings && MovementComponent)
	{
		Settings = MovementComponent->MovementSettings;
	}
}

namespace
{
	/** Потолок шага для пружин камеры. Заметно ниже порога устойчивости самой жёсткой из них. */
	constexpr float MaxShakeStep = 1.0f / 60.0f;

	/** Смещение камеры больше этого - уже не тряска, а авария. */
	constexpr float MaxSaneShakeOffset = 200.0f;
}

void UCameraShakeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CameraComponent) return;

	// Lazy init
	if (!Settings && MovementComponent && MovementComponent->MovementSettings)
	{
		Settings = MovementComponent->MovementSettings;
		BaseFOV = CameraComponent->FieldOfView;
	}

	if (!OwnerController)
	{
		if (AActor* Owner = GetOwner())
		{
			if (APawn* Pawn = Cast<APawn>(Owner))
			{
				OwnerController = Cast<APlayerController>(Pawn->GetController());
			}
		}
	}

	if (!Settings) return;

	// Большой кадр разносит пружины, поэтому шаг ограничен сверху.
	//
	// FBobSpringState интегрируется явным методом Эйлера. У него есть порог устойчивости: при
	// dt больше примерно 2/sqrt(Stiffness) шаг перелетает цель дальше, чем был от неё, и амплитуда
	// растёт с каждым кадром. При Stiffness = 80 это ~0.1 с.
	//
	// Alt+Tab в PIE даёт ровно такие кадры: редактор душит игру в фоне до нескольких кадров в
	// секунду. Пружина за это время расходится экспоненциально по числу кадров, интенсивность бобa
	// вырастает до астрономических чисел, и камера уезжает далеко за карту. Возвращается она
	// примерно за то же время, сколько её не трогали, потому что расходилась экспоненциально, а
	// сходится с постоянной скоростью - отсюда и симметрия, которая сбивала с толку.
	//
	// На ускоренном времени (polarity.debug.timescale) кадр домножается ещё раз, чисел не хватает,
	// и в пружине появляется NaN. Дальше он уезжает в относительную позицию камеры, а с ней в
	// перволичный меш, и падает уже анимация: Assertion failed: !Pose[BoneIndex].ContainsNaN().
	//
	// Ограничение, а не подшаг: боб чисто косметический, и отстать на пару кадров после хитча ему
	// можно, а вот считать его несколько раз за кадр незачем.
	DeltaTime = FMath::Min(DeltaTime, MaxShakeStep);

	// Reset
	CurrentOffset = FVector::ZeroVector;
	CurrentRotationOffset = FRotator::ZeroRotator;
	CurrentFOVOffset = 0.0f;
	CurrentViewmodelBobOffset = FVector::ZeroVector;
	CurrentViewmodelBobRotation = FRotator::ZeroRotator;

	if (Settings->bEnableCameraShake)
	{
		// Update all shake instances
		UpdateActiveShakes(DeltaTime);

		// Update procedural walk/sprint bob (Titanfall 2/Apex style)
		UpdateProceduralBob(DeltaTime);

		// Update continuous effects
		UpdateSlideShake(DeltaTime);
		UpdateWallrunBob(DeltaTime);
		UpdateWallrunFOV(DeltaTime);
		UpdateAirDashFOV(DeltaTime);
		UpdateFocusFOV(DeltaTime);
	}

	// Apply UNCONDITIONALLY, even with shake disabled. This component is the only writer of
	// FieldOfView on the first person camera and the only mirror of FirstPersonFieldOfView, so
	// bailing out above left the ADS blend with nowhere to land: AShooterCharacter::UpdateADS
	// handed its blended FOV to SetBaseFOV() and nobody ever pushed it to the camera. The symptom
	// was "aiming does not zoom at all", far away from the shake toggle that actually caused it.
	// Страховка на случай, если что-то всё-таки досчиталось до NaN или до бессмыслицы: лучше
	// потерять боб на кадр, чем отдать NaN дальше по цепочке. Она заканчивается позой скелета, и
	// там это уже не артефакт, а падение движка.
	const bool bSane =
		!CurrentOffset.ContainsNaN() && !CurrentRotationOffset.ContainsNaN() &&
		FMath::IsFinite(CurrentFOVOffset) &&
		CurrentOffset.SizeSquared() < FMath::Square(MaxSaneShakeOffset);
	if (!bSane)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CAMERA_DEBUG] Пружины камеры разошлись, сброшены"));
		ProceduralBob.IntensitySpring.Reset();
		ProceduralBob.SprintBlendSpring.Reset();
		CurrentOffset = FVector::ZeroVector;
		CurrentRotationOffset = FRotator::ZeroRotator;
		CurrentFOVOffset = 0.0f;
		CurrentViewmodelBobOffset = FVector::ZeroVector;
		CurrentViewmodelBobRotation = FRotator::ZeroRotator;
	}

	// With every offset zeroed just above, this path is simply "camera FOV = BaseFOV".
	ApplyToCamera(DeltaTime);
}

// ==================== Damped Oscillator Core ====================

float UCameraShakeComponent::UpdateDampedOscillator(FDampedOscillator& Osc, float DeltaTime)
{
	if (!Osc.bActive) return 0.0f;

	Osc.Time += DeltaTime;

	// x(t) = A * e^(-damping * t) * sin(2π * frequency * t)
	float Decay = FMath::Exp(-Osc.Damping * Osc.Time);
	float Phase = Osc.Time * Osc.Frequency * 2.0f * PI;
	float Value = Osc.Amplitude * Decay * FMath::Sin(Phase);

	// Stop when amplitude is negligible
	if (Decay < 0.01f)
	{
		Osc.bActive = false;
		return 0.0f;
	}

	return Value;
}

void UCameraShakeComponent::TriggerOscillator(FDampedOscillator& Osc, float InAmplitude, float InFrequency, float InDamping)
{
	Osc.bActive = true;
	Osc.Time = 0.0f;
	Osc.Amplitude = InAmplitude;
	Osc.Frequency = InFrequency;
	Osc.Damping = InDamping;
}

// ==================== Active Shakes Management ====================

void UCameraShakeComponent::UpdateActiveShakes(float DeltaTime)
{
	// Landing shake - pitch oscillation
	if (LandingPitchOsc.bActive)
	{
		float PitchValue = UpdateDampedOscillator(LandingPitchOsc, DeltaTime);
		CurrentRotationOffset.Pitch += PitchValue;
	}

	// Landing shake - Z position oscillation (body compression)
	if (LandingZOsc.bActive)
	{
		float ZValue = UpdateDampedOscillator(LandingZOsc, DeltaTime);
		CurrentOffset.Z += ZValue;
	}

	// Jump shake - pitch
	if (JumpPitchOsc.bActive)
	{
		float PitchValue = UpdateDampedOscillator(JumpPitchOsc, DeltaTime);
		CurrentRotationOffset.Pitch += PitchValue;
	}
}

// ==================== Procedural Bob (Titanfall 2/Apex) ====================

void UCameraShakeComponent::UpdateProceduralBob(float DeltaTime)
{
	if (!ProceduralBob.bEnabled || !MovementComponent) return;

	// Calculate target intensity
	float TargetIntensity = 0.0f;
	if (!bBobSuppressed && IsMovingOnGround())
	{
		float Speed = GetHorizontalSpeed();
		if (Speed >= ProceduralBob.MinSpeedForBob)
		{
			float Alpha = FMath::Clamp(
				(Speed - ProceduralBob.MinSpeedForBob) / (ProceduralBob.FullIntensitySpeed - ProceduralBob.MinSpeedForBob),
				0.0f, 1.0f
			);
			TargetIntensity = FMath::SmoothStep(0.0f, 1.0f, Alpha);
		}
	}

	// Update springs
	ProceduralBob.IntensitySpring.Update(TargetIntensity, ProceduralBob.IntensitySpringStiffness, DeltaTime);

	float TargetSprintBlend = IsSprinting() ? 1.0f : 0.0f;
	ProceduralBob.SprintBlendSpring.Update(TargetSprintBlend, ProceduralBob.StateSpringStiffness, DeltaTime);

	// Calculate phase increment
	float PhaseIncrement = 0.0f;
	if (ProceduralBob.IntensitySpring.Value > 0.01f && IsMovingOnGround())
	{
		float Speed = GetHorizontalSpeed();
		float SprintBlend = ProceduralBob.SprintBlendSpring.Value;
		float StepDistance = FMath::Lerp(ProceduralBob.WalkStepDistance, ProceduralBob.SprintStepDistance, SprintBlend);
		float Frequency = Speed / FMath::Max(StepDistance, 1.0f);
		PhaseIncrement = 2.0f * PI * Frequency * DeltaTime;
	}

	// Calculate amplitudes with sprint blend
	float SprintBlend = ProceduralBob.SprintBlendSpring.Value;
	float HorizAmp = FMath::Lerp(ProceduralBob.WalkHorizontalAmplitude,
		ProceduralBob.WalkHorizontalAmplitude * ProceduralBob.SprintHorizontalMultiplier,
		SprintBlend);
	float VertAmp = FMath::Lerp(ProceduralBob.WalkVerticalAmplitude,
		ProceduralBob.WalkVerticalAmplitude * ProceduralBob.SprintVerticalMultiplier,
		SprintBlend);
	float RollAmp = FMath::Lerp(ProceduralBob.WalkRollAmplitude,
		ProceduralBob.WalkRollAmplitude * ProceduralBob.SprintRollMultiplier,
		SprintBlend);
	float PitchAmp = FMath::Lerp(ProceduralBob.WalkPitchAmplitude,
		ProceduralBob.WalkPitchAmplitude * ProceduralBob.SprintPitchMultiplier,
		SprintBlend);

	// Final intensity with preset and global multipliers
	float FinalIntensity = ProceduralBob.IntensitySpring.Value * ProceduralBob.GetPresetMultiplier() * ProceduralBob.GlobalIntensity;

	// Update bob generator with wave shaping
	ProceduralBob.BobGenerator.Update(PhaseIncrement, FinalIntensity, HorizAmp, VertAmp, RollAmp, PitchAmp,
		ProceduralBob.Aggressiveness, ProceduralBob.bEnableAsymmetry, ProceduralBob.AsymmetryStrength);

	// Apply to camera output (scaled by preset)
	float CameraScale = ProceduralBob.GetCameraScale();

	CurrentOffset.Y += ProceduralBob.BobGenerator.HorizontalOffset * CameraScale;
	CurrentOffset.Z += ProceduralBob.BobGenerator.VerticalOffset * CameraScale;
	CurrentRotationOffset.Pitch += ProceduralBob.BobGenerator.PitchOffset * CameraScale;
	CurrentRotationOffset.Roll += ProceduralBob.BobGenerator.RollOffset * CameraScale;

	// Calculate viewmodel output (if enabled)
	if (ProceduralBob.bEnableViewmodelBob)
	{
		float VMScale = ProceduralBob.ViewmodelBobMultiplier;

		// Forward sway for figure-8 depth
		float ForwardSway = FMath::Sin(ProceduralBob.BobGenerator.Phase + PI * 0.5f) *
			ProceduralBob.ViewmodelForwardSway * FinalIntensity;

		CurrentViewmodelBobOffset = FVector(
			ForwardSway,
			ProceduralBob.BobGenerator.HorizontalOffset * VMScale,
			ProceduralBob.BobGenerator.VerticalOffset * VMScale
		);

		CurrentViewmodelBobRotation = FRotator(
			ProceduralBob.BobGenerator.PitchOffset * VMScale,
			0.0f,
			ProceduralBob.BobGenerator.RollOffset * VMScale
		);
	}
}

// ==================== Event Triggers ====================

void UCameraShakeComponent::TriggerLandingShake(float FallVelocity)
{
	if (!Settings || !Settings->bEnableLandingShake) return;

	float AbsVelocity = FMath::Abs(FallVelocity);
	if (AbsVelocity < Settings->LandingShakeMinVelocity) return;

	// Calculate intensity based on fall velocity
	float Alpha = FMath::Clamp(
		(AbsVelocity - Settings->LandingShakeMinVelocity) /
		(Settings->LandingShakeMaxVelocity - Settings->LandingShakeMinVelocity),
		0.0f, 1.0f
	);

	float Intensity = Alpha * Settings->CameraShakeIntensity;

	// Pitch oscillation (looking down then oscillating back)
	float PitchAmp = -Settings->LandingShakeMaxPitch * Intensity;
	float PitchFreq = Settings->LandingShakeFrequency;
	float PitchDamp = Settings->LandingShakeDamping;

	TriggerOscillator(LandingPitchOsc, PitchAmp, PitchFreq, PitchDamp);

	// Z position oscillation (body compression)
	float ZAmp = -Settings->LandingShakeZAmplitude * Intensity;
	TriggerOscillator(LandingZOsc, ZAmp, PitchFreq * 1.2f, PitchDamp * 1.5f);
}

void UCameraShakeComponent::TriggerJumpShake(bool bIsDoubleJump)
{
	if (!Settings || !Settings->bEnableJumpShake) return;

	float Multiplier = bIsDoubleJump ? Settings->DoubleJumpKickMultiplier : 1.0f;
	float Intensity = Multiplier * Settings->CameraShakeIntensity;

	float PitchAmp = Settings->JumpCameraKick * Intensity;
	float PitchFreq = Settings->JumpShakeFrequency;
	float PitchDamp = Settings->JumpShakeDamping;

	TriggerOscillator(JumpPitchOsc, PitchAmp, PitchFreq, PitchDamp);
}

void UCameraShakeComponent::TriggerSlideStart()
{
	bIsSliding = true;
	SlideTime = 0.0f;
	SlideIntensity = 0.0f;
}

void UCameraShakeComponent::TriggerSlideEnd()
{
	bIsSliding = false;
}

void UCameraShakeComponent::TriggerAirDash()
{
	if (!Settings || !Settings->bEnableAirDashShake) return;

	AirDashFOVTime = Settings->AirDashFOVDuration;
	AirDashFOVIntensity = 1.0f;
}

void UCameraShakeComponent::TriggerWallrunStart()
{
	bIsWallrunning = true;
	WallrunBobTime = 0.0f;
}

void UCameraShakeComponent::TriggerWallrunEnd()
{
	bIsWallrunning = false;
}

// ==================== Continuous Effects ====================

void UCameraShakeComponent::UpdateSlideShake(float DeltaTime)
{
	if (!Settings) return;

	// The ramp feeds BOTH the shake and the speed FOV, so it is updated before either toggle is
	// consulted. Gating it on bEnableSlideShake would freeze the FOV effect at zero for anyone who
	// turned the shake off.
	float TargetIntensity = bIsSliding ? 1.0f : 0.0f;
	SlideIntensity = FMath::FInterpTo(SlideIntensity, TargetIntensity, DeltaTime, 10.0f);

	if (SlideIntensity < 0.01f) return;

	// Speed FOV: additive degrees on top of the blended base, never an absolute angle. That is the
	// whole point — an absolute number would silently overwrite the player's FOV setting, while an
	// offset means "open up a bit more than wherever you were", which is true at every setting.
	if (Settings->bEnableSlideFOV)
	{
		CurrentFOVOffset += Settings->SlideFOVAdd * SlideIntensity * Settings->CameraShakeIntensity;
	}

	if (!Settings->bEnableSlideShake) return;

	SlideTime += DeltaTime;

	// Multi-frequency noise for organic feel
	float NoiseX = PerlinNoise1D(SlideTime * Settings->SlideShakeFrequency);
	float NoiseY = PerlinNoise1D(SlideTime * Settings->SlideShakeFrequency + 100.0f);

	float Intensity = Settings->SlideShakeIntensity * SlideIntensity * Settings->CameraShakeIntensity;

	CurrentRotationOffset.Pitch += NoiseX * Intensity + Settings->SlideCameraPitch * SlideIntensity;
	CurrentRotationOffset.Roll += NoiseY * Intensity * 0.5f;
}

void UCameraShakeComponent::UpdateWallrunBob(float DeltaTime)
{
	if (!Settings || !Settings->bEnableWallrunBob || !bIsWallrunning) return;

	WallrunBobTime += DeltaTime;

	float Freq = Settings->WallrunBobFrequency;
	float Amp = Settings->WallrunBobAmplitude * Settings->CameraShakeIntensity;

	// Running motion - vertical bob with slight horizontal sway
	float VerticalPhase = WallrunBobTime * Freq * 2.0f * PI;
	float VerticalBob = FMath::Sin(VerticalPhase) * Amp;

	float HorizontalPhase = WallrunBobTime * Freq * PI;
	float HorizontalBob = FMath::Sin(HorizontalPhase) * Amp * 0.3f;

	float ForwardPhase = WallrunBobTime * Freq * 2.0f * PI + PI * 0.5f;
	float ForwardBob = FMath::Sin(ForwardPhase) * Amp * 0.15f;

	CurrentOffset.Z += VerticalBob;
	CurrentOffset.Y += HorizontalBob;
	CurrentOffset.X += ForwardBob;

	float RollBob = FMath::Sin(HorizontalPhase) * 0.5f * Settings->CameraShakeIntensity;
	CurrentRotationOffset.Roll += RollBob;
}

void UCameraShakeComponent::UpdateWallrunFOV(float DeltaTime)
{
	if (!Settings || !Settings->bEnableWallrunFOV) return;

	float TargetIntensity = bIsWallrunning ? 1.0f : 0.0f;

	WallrunFOVIntensity = FMath::FInterpTo(
		WallrunFOVIntensity,
		TargetIntensity,
		DeltaTime,
		Settings->WallrunFOVInterpSpeed
	);

	CurrentFOVOffset += Settings->WallrunFOVAdd * WallrunFOVIntensity * Settings->CameraShakeIntensity;
}

void UCameraShakeComponent::UpdateAirDashFOV(float DeltaTime)
{
	if (!Settings) return;

	if (AirDashFOVTime > 0.0f)
	{
		AirDashFOVTime -= DeltaTime;
	}
	else
	{
		AirDashFOVIntensity = FMath::FInterpTo(AirDashFOVIntensity, 0.0f, DeltaTime, 5.0f);
	}

	CurrentFOVOffset += Settings->AirDashFOVAdd * AirDashFOVIntensity * Settings->CameraShakeIntensity;
}

void UCameraShakeComponent::UpdateFocusFOV(float DeltaTime)
{
	if (!Settings || !Settings->bEnableFocusFOV) return;

	// Ramped rather than switched, and NOT gated behind an early return of its own: the ramp has to
	// keep running on the way back down, or letting go of the lock would leave the view zoomed in.
	FocusFOVIntensity = FMath::FInterpTo(
		FocusFOVIntensity,
		bIsFocusLocked ? 1.0f : 0.0f,
		DeltaTime,
		FMath::Max(1.0f, Settings->FocusFOVBlendSpeed)
	);

	if (FocusFOVIntensity < 0.001f) return;

	// FocusFOVAdd is negative: this SUBTRACTS from the view. Not scaled by CameraShakeIntensity,
	// unlike the speed effects -- this one is aiming feedback rather than shake, and a player who
	// turned the shake down still needs to see which enemy the blade has picked.
	CurrentFOVOffset += Settings->FocusFOVAdd * FocusFOVIntensity;
}

// ==================== Apply to Camera ====================

void UCameraShakeComponent::ApplyToCamera(float DeltaTime)
{
	if (!CameraComponent) return;

	// FOV. BaseFOV is the aim-blended FOV handed over by AShooterCharacter::UpdateADS, already
	// expressed in the player's own FOV setting. The speed effects (slide, wallrun, air dash) ride
	// on top of it as plain degrees, faded out by FOVEffectScale so that a slide taken while aiming
	// does not push the scope back open: +12 degrees is a nudge on a 90 degree hipfire view and a
	// wrecking ball on a 45 degree sight picture.
	float TargetFOV = BaseFOV + CurrentFOVOffset * FOVEffectScale;
	if (FMath::Abs(CameraComponent->FieldOfView - TargetFOV) > 0.1f)
	{
		CameraComponent->SetFieldOfView(TargetFOV);
	}

	// FirstPersonFieldOfView is deliberately NOT touched here. The viewmodel has its own fixed FOV,
	// set on the camera component, the way Apex, CoD and CS keep the gun one size whatever the
	// player's FOV slider says. Mirroring the world FOV into it used to stretch the hands along
	// with the FOV setting and with every speed kick above (slide, wallrun, air dash).
	//
	// The catch that made it look like the two HAD to stay equal: anything drawn next to the gun
	// but not marked FirstPersonPrimitiveType is projected with the world FOV and slides off the
	// weapon as soon as the two differ. Fix the marking on that component, not the FOV.
}

// ==================== Helpers ====================

float UCameraShakeComponent::GetSpeedRatio() const
{
	if (!MovementComponent || !Settings) return 0.0f;
	return MovementComponent->GetSpeedRatio();
}

bool UCameraShakeComponent::IsMovingOnGround() const
{
	if (!MovementComponent) return false;
	return MovementComponent->IsMovingOnGround() &&
		!MovementComponent->IsSliding() &&
		!MovementComponent->IsWallRunning();
}

bool UCameraShakeComponent::IsSprinting() const
{
	if (!MovementComponent) return false;
	return MovementComponent->IsSprinting();
}

float UCameraShakeComponent::GetHorizontalSpeed() const
{
	if (!MovementComponent) return 0.0f;
	FVector Velocity = MovementComponent->Velocity;
	Velocity.Z = 0.0f;
	return Velocity.Size();
}

float UCameraShakeComponent::PerlinNoise1D(float X) const
{
	// Multi-octave pseudo-perlin noise
	float Result = 0.0f;
	Result += FMath::Sin(X * 1.0f) * 0.5f;
	Result += FMath::Sin(X * 2.3f + 1.3f) * 0.25f;
	Result += FMath::Sin(X * 4.1f + 2.7f) * 0.125f;
	Result += FMath::Sin(X * 7.9f + 4.1f) * 0.0625f;
	return Result;
}