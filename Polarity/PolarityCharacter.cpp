// Copyright Epic Games, Inc. All Rights Reserved.

#include "PolarityCharacter.h"
#include "ApexMovementComponent.h"
#include "MovementSettings.h"
#include "CameraShakeComponent.h"
#include "ChargeAnimationComponent.h"
#include "PolarityCameraManager.h"
#include "EMFVelocityModifier.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "Curves/CurveVector.h"
#include "UObject/Class.h"
#include "Polarity.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

APolarityCharacter::APolarityCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UApexMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	ApexMovement = Cast<UApexMovementComponent>(GetCharacterMovement());

	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
	FirstPersonCameraComponent->SetRelativeRotation(FRotator::ZeroRotator);
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	CameraShakeComponent = CreateDefaultSubobject<UCameraShakeComponent>(TEXT("Camera Shake"));

	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;
	GetCapsuleComponent()->SetCapsuleSize(34.0f, 88.0f);

	GetCharacterMovement()->BrakingDecelerationFalling = 0.0f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->GravityScale = 1.17f;
}

void APolarityCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (ApexMovement && MovementSettings)
	{
		ApexMovement->MovementSettings = MovementSettings;
	}

	// Store base transform of FirstPersonMesh for offset calculations
	if (FirstPersonMesh)
	{
		FirstPersonMeshBaseLocation = FirstPersonMesh->GetRelativeLocation();
		FirstPersonMeshBaseRotation = FirstPersonMesh->GetRelativeRotation();
	}

	// Initialize camera shake
	if (CameraShakeComponent)
	{
		if (FirstPersonCameraComponent)
		{
			// Store base camera rotation for roll effects
			BaseCameraRotation = FirstPersonCameraComponent->GetRelativeRotation();
		}

		CameraShakeComponent->Initialize(FirstPersonCameraComponent, ApexMovement, MovementSettings);
	}

	// Bind to movement events
	if (ApexMovement)
	{
		ApexMovement->OnLanded_Movement.AddDynamic(this, &APolarityCharacter::OnMovementLanded);
		ApexMovement->OnSlideStarted.AddDynamic(this, &APolarityCharacter::OnSlideStarted);
		ApexMovement->OnSlideEnded.AddDynamic(this, &APolarityCharacter::OnSlideEnded);
		ApexMovement->OnWallrunStarted.AddDynamic(this, &APolarityCharacter::OnWallrunStarted);
		ApexMovement->OnWallrunEnded.AddDynamic(this, &APolarityCharacter::OnWallrunEnded);
	}
}

void APolarityCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateCameraEffects(DeltaTime);
	UpdateFirstPersonView(DeltaTime);
	UpdateProceduralFootsteps(DeltaTime);

	// Check for jump to trigger shake
	if (ApexMovement)
	{
		int32 CurrentJumpCount = ApexMovement->CurrentJumpCount;
		if (CurrentJumpCount > LastJumpCount)
		{
			bool bIsDoubleJump = CurrentJumpCount > 1;
			if (CameraShakeComponent)
			{
				CameraShakeComponent->TriggerJumpShake(bIsDoubleJump);
			}
		}
		LastJumpCount = CurrentJumpCount;
	}
}

void APolarityCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &APolarityCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &APolarityCharacter::DoJumpEnd);

		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APolarityCharacter::MoveInput);
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &APolarityCharacter::MoveInput);

		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &APolarityCharacter::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &APolarityCharacter::LookInput);

		if (SprintAction)
		{
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &APolarityCharacter::SprintStart);
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &APolarityCharacter::SprintStop);
		}

		if (CrouchSlideAction)
		{
			EnhancedInputComponent->BindAction(CrouchSlideAction, ETriggerEvent::Started, this, &APolarityCharacter::CrouchSlideStart);
			EnhancedInputComponent->BindAction(CrouchSlideAction, ETriggerEvent::Completed, this, &APolarityCharacter::CrouchSlideStop);
		}

		if (ToggleChargeAction)
		{
			EnhancedInputComponent->BindAction(ToggleChargeAction, ETriggerEvent::Started, this, &APolarityCharacter::DoToggleCharge);
		}
	}
}

void APolarityCharacter::MoveInput(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();
	CurrentMoveInput = MovementVector;

	if (ApexMovement)
	{
		ApexMovement->SetMoveInput(MovementVector);
	}

	DoMove(MovementVector.X, MovementVector.Y);
}

void APolarityCharacter::LookInput(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();
	DoAim(LookAxisVector.X, LookAxisVector.Y);
}

void APolarityCharacter::DoAim(float Yaw, float Pitch)
{
	if (GetController())
	{
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void APolarityCharacter::DoMove(float Right, float Forward)
{
	if (GetController())
	{
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void APolarityCharacter::DoJumpStart()
{
	if (ApexMovement)
	{
		ApexMovement->TryJump();
	}
	else
	{
		Jump();
	}
}

void APolarityCharacter::DoJumpEnd()
{
	StopJumping();
}

void APolarityCharacter::SprintStart(const FInputActionValue& Value)
{
	if (ApexMovement)
	{
		ApexMovement->StartSprint();
	}
}

void APolarityCharacter::SprintStop(const FInputActionValue& Value)
{
	if (ApexMovement)
	{
		ApexMovement->StopSprint();
	}
}

void APolarityCharacter::CrouchSlideStart(const FInputActionValue& Value)
{
	if (ApexMovement)
	{
		// Check if we're about to air dash (for FOV effect)
		bool bWillAirDash = ApexMovement->IsFalling() && ApexMovement->CanAirDash();

		ApexMovement->TryCrouchSlide();

		if (bWillAirDash && CameraShakeComponent)
		{
			CameraShakeComponent->TriggerAirDash();
		}
	}
	else
	{
		Crouch();
	}
}

void APolarityCharacter::CrouchSlideStop(const FInputActionValue& Value)
{
	if (ApexMovement)
	{
		ApexMovement->StopCrouchSlide();
	}
	else
	{
		UnCrouch();
	}
}

void APolarityCharacter::UpdateCameraEffects(float DeltaTime)
{
	// Camera roll logic moved to UpdateFirstPersonView for unified handling
}

// ==================== Movement Event Handlers ====================

void APolarityCharacter::OnMovementLanded(const FHitResult& Hit)
{
	if (CameraShakeComponent && ApexMovement)
	{
		float FallVelocity = FMath::Abs(ApexMovement->LastFallVelocity);
		CameraShakeComponent->TriggerLandingShake(FallVelocity);
	}

	LastJumpCount = 0;
}

void APolarityCharacter::OnSlideStarted()
{
	if (CameraShakeComponent)
	{
		CameraShakeComponent->TriggerSlideStart();
	}
}

void APolarityCharacter::OnSlideEnded()
{
	if (CameraShakeComponent)
	{
		CameraShakeComponent->TriggerSlideEnd();
	}
}

void APolarityCharacter::OnWallrunStarted(EWallSide Side)
{
	if (CameraShakeComponent)
	{
		CameraShakeComponent->TriggerWallrunStart();
	}
}

void APolarityCharacter::OnWallrunEnded()
{
	if (CameraShakeComponent)
	{
		CameraShakeComponent->TriggerWallrunEnd();
	}
}

// ==================== EMF System ====================

void APolarityCharacter::SetCharge(float NewCharge)
{
	CurrentCharge = FMath::Clamp(NewCharge, -1.0f, 1.0f);
}

void APolarityCharacter::AddCharge(float Delta)
{
	SetCharge(CurrentCharge + Delta);
}

void APolarityCharacter::DoToggleCharge()
{
	UEMFVelocityModifier* EMFModifier = FindComponentByClass<UEMFVelocityModifier>();
	UChargeAnimationComponent* ChargeAnim = FindComponentByClass<UChargeAnimationComponent>();

	// Try to start charge animation
	if (ChargeAnim && ChargeAnim->CanStartAnimation())
	{
		if (ChargeAnim->StartChargeAnimation())
		{
			// Toggle charge when animation starts playing (after mesh transition)
			if (EMFModifier)
			{
				EMFModifier->ToggleChargeSign();
			}
			return;
		}
	}

	// Fallback: toggle without animation if component not available
	if (EMFModifier)
	{
		EMFModifier->ToggleChargeSign();
	}
}

// ==================== First Person View ====================

void APolarityCharacter::UpdateFirstPersonView(float DeltaTime)
{
	if (!FirstPersonMesh || !MovementSettings)
	{
		return;
	}

	// ==================== Crouch/Slide Camera Offset ====================

	FVector TargetCrouchOffset = FVector::ZeroVector;

	bool bIsSliding = false;
	bool bIsCrouching = false;
	bool bIsWallrunning = false;

	if (ApexMovement)
	{
		bIsSliding = ApexMovement->IsSliding();
		bIsCrouching = ApexMovement->IsCrouching();
		bIsWallrunning = ApexMovement->IsWallRunning();
	}

	if (MovementSettings->bEnableFirstPersonOffset && ApexMovement)
	{
		if (bIsSliding)
		{
			TargetCrouchOffset = MovementSettings->SlideCameraOffset;
		}
		else if (bIsCrouching)
		{
			TargetCrouchOffset = MovementSettings->CrouchCameraOffset;
		}
	}

	// Interpolate crouch offset
	CurrentCrouchOffset = FMath::VInterpTo(
		CurrentCrouchOffset,
		TargetCrouchOffset,
		DeltaTime,
		MovementSettings->CameraZOffsetInterpSpeed
	);

	// ==================== Mesh Tilt (Crouch/Slide/Wallrun) ====================

	FRotator TargetMeshTilt = FRotator::ZeroRotator;

	// Crouch/Slide/Wallrun tilt - applied to weapon mesh
	if (MovementSettings->bEnableWeaponTilt && ApexMovement)
	{
		if (bIsSliding)
		{
			TargetMeshTilt.Roll = MovementSettings->SlideWeaponTiltRoll;
			TargetMeshTilt.Pitch = MovementSettings->SlideWeaponTiltPitch;
		}
		else if (bIsCrouching)
		{
			TargetMeshTilt.Roll = MovementSettings->CrouchWeaponTiltRoll;
			TargetMeshTilt.Pitch = MovementSettings->CrouchWeaponTiltPitch;
		}
		else if (bIsWallrunning)
		{
			// Use the pre-calculated mesh tilt from ApexMovement (same logic as camera)
			TargetMeshTilt.Roll = ApexMovement->CurrentWallRunMeshRoll;
			TargetMeshTilt.Pitch = ApexMovement->CurrentWallRunMeshPitch;

			UE_LOG(LogTemplateCharacter, Warning, TEXT("WallRun Mesh: Side=%s, MeshPitch=%.2f, CameraRoll=%.2f"),
				ApexMovement->WallRunSide == EWallSide::Left ? TEXT("Left") : TEXT("Right"),
				ApexMovement->CurrentWallRunMeshPitch,
				ApexMovement->CurrentWallRunCameraRoll);
		}
	}

	// Wallrun camera offset - use the pre-calculated offset from ApexMovement
	if (bIsWallrunning && ApexMovement)
	{
		TargetWallrunOffset = ApexMovement->CurrentWallRunCameraOffset;
	}
	else
	{
		// Reset offset when not wallrunning
		TargetWallrunOffset = FVector::ZeroVector;
	}

	// Shake roll from camera shake component - applied to weapon mesh
	if (CameraShakeComponent)
	{
		TargetMeshTilt.Roll += CameraShakeComponent->GetCameraRotationOffset().Roll;
	}

	// Interpolate mesh tilt
	CurrentWeaponTilt = FMath::RInterpTo(
		CurrentWeaponTilt,
		TargetMeshTilt,
		DeltaTime,
		MovementSettings->WeaponTiltInterpSpeed
	);

	// ==================== Camera Roll (Wallrun) ====================

	// Wallrun roll is applied ONLY to camera, not to weapon mesh
	// This prevents the weapon from clipping through walls
	// Camera roll is already interpolated in ApexMovement, use it directly
	float WallrunCameraRoll = 0.0f;
	if (ApexMovement)
	{
		// Use the new pre-calculated camera roll (already has correct direction applied)
		WallrunCameraRoll = ApexMovement->CurrentWallRunCameraRoll;

		// Debug: log when wallrunning
		if (ApexMovement->IsWallRunning())
		{
			UE_LOG(LogTemplateCharacter, Verbose, TEXT("Wallrun Camera Roll: %.2f"), WallrunCameraRoll);
		}
	}


	// ==================== Wallrun Offset ====================

	// Interpolate ADS offset (set by ShooterCharacter)
	CurrentWallrunOffset = FMath::VInterpTo(
		CurrentWallrunOffset,
		TargetWallrunOffset,
		DeltaTime,
		MovementSettings->ADSInterpSpeed
	);


	// ==================== ADS Offset ====================

	// Interpolate ADS offset (set by ShooterCharacter)
	CurrentADSOffset = FMath::VInterpTo(
		CurrentADSOffset,
		TargetADSOffset,
		DeltaTime,
		MovementSettings->ADSInterpSpeed
	);

	// ==================== Weapon Run Sway ====================

	UpdateWeaponRunSway(DeltaTime);

	// ==================== Apply to FirstPersonMesh ====================

	// Calculate final location
	FVector NewLocation = FirstPersonMeshBaseLocation;
	NewLocation += CurrentCrouchOffset;
	NewLocation += (CurrentADSOffset + CurrentWallrunOffset);
	NewLocation += CurrentRunSwayPosition; // Run sway position offset

	// Calculate final rotation (base + all tilts combined)
	FRotator NewRotation = FirstPersonMeshBaseRotation;
	NewRotation.Pitch += CurrentWeaponTilt.Pitch;
	NewRotation.Yaw += CurrentWeaponTilt.Yaw;
	NewRotation.Roll += CurrentWeaponTilt.Roll;
	// Add run sway rotation
	NewRotation.Pitch += CurrentRunSwayRotation.Pitch;
	NewRotation.Yaw += CurrentRunSwayRotation.Yaw;
	NewRotation.Roll += CurrentRunSwayRotation.Roll;

	// Apply transform to mesh
	FirstPersonMesh->SetRelativeLocation(NewLocation);
	FirstPersonMesh->SetRelativeRotation(NewRotation);

	// ==================== Apply Camera Roll via CameraManager ====================

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (APolarityCameraManager* CamManager = Cast<APolarityCameraManager>(PC->PlayerCameraManager))
		{
			// Camera gets: wallrun roll + shake roll
			// Weapon mesh gets: wallrun mesh tilt + crouch/slide tilt + shake roll (applied above)
			float ShakeRoll = CameraShakeComponent ? CameraShakeComponent->GetCameraRotationOffset().Roll : 0.0f;

			// WallrunCameraRoll already has direction applied, no need to multiply
			CamManager->TargetRotationOffset.Roll = WallrunCameraRoll + ShakeRoll;

			// Debug
			if (FMath::Abs(WallrunCameraRoll) > 0.1f)
			{
				UE_LOG(LogTemplateCharacter, Verbose, TEXT("CameraManager TargetRoll=%.2f (Wallrun=%.2f, Shake=%.2f)"),
					CamManager->TargetRotationOffset.Roll, WallrunCameraRoll, ShakeRoll);
			}
		}
		else
		{
			UE_LOG(LogTemplateCharacter, Error, TEXT("PolarityCameraManager not found!"));
		}
	}

	// ==================== Update Aim Offset for AnimBP ====================
	UpdateAnimInstanceAimOffset(DeltaTime);
}

// ==================== Procedural Footsteps ====================

void APolarityCharacter::UpdateProceduralFootsteps(float DeltaTime)
{
	if (!MovementSettings || !MovementSettings->bEnableProceduralFootsteps)
	{
		return;
	}

	if (!ApexMovement)
	{
		return;
	}

	// Determine footstep interval based on movement state
	float FootstepInterval = 0.0f;
	bool bShouldPlayFootsteps = false;
	bool bIsWallrun = false;

	if (ApexMovement->IsWallRunning())
	{
		FootstepInterval = MovementSettings->FootstepWallrunInterval;
		bShouldPlayFootsteps = true;
		bIsWallrun = true;
	}
	else if (ApexMovement->IsMovingOnGround() && !ApexMovement->IsSliding())
	{
		float SpeedRatio = ApexMovement->GetSpeedRatio();

		// Only play if moving fast enough
		if (SpeedRatio >= MovementSettings->FootstepMinSpeedRatio)
		{
			bShouldPlayFootsteps = true;

			if (ApexMovement->IsSprinting())
			{
				FootstepInterval = MovementSettings->FootstepSprintInterval;
			}
			else
			{
				FootstepInterval = MovementSettings->FootstepWalkInterval;
			}

			// Adjust interval based on actual speed (faster movement = faster footsteps)
			FootstepInterval /= FMath::Max(SpeedRatio, 0.5f);
		}
	}

	if (!bShouldPlayFootsteps)
	{
		// Reset timer when not moving
		FootstepTimer = 0.0f;
		return;
	}

	// Update timer
	FootstepTimer += DeltaTime;

	// Play footstep when timer exceeds interval
	if (FootstepTimer >= FootstepInterval)
	{
		FootstepTimer = 0.0f;

		// Play sound and alternate feet
		PlayProceduralFootstep(bIsWallrun, bIsLeftFoot);
		bIsLeftFoot = !bIsLeftFoot;
	}
}

void APolarityCharacter::PlayProceduralFootstep_Implementation(bool bIsWallrun, bool bLeftFoot)
{
	// Select sound based on wallrun state
	USoundBase* SoundToPlay = bIsWallrun ? ProceduralWallrunFootstepSound : ProceduralFootstepSound;

	if (!SoundToPlay)
	{
		return;
	}

	// Calculate volume and pitch
	float Volume = MovementSettings ? MovementSettings->FootstepVolume : 1.0f;
	float PitchVariation = MovementSettings ? MovementSettings->FootstepPitchVariation : 0.1f;
	float Pitch = 1.0f + FMath::RandRange(-PitchVariation, PitchVariation);

	// Play sound at character location
	UGameplayStatics::PlaySoundAtLocation(
		this,
		SoundToPlay,
		GetActorLocation(),
		Volume,
		Pitch
	);
}

// ==================== Weapon Run Sway ====================

void APolarityCharacter::UpdateWeaponRunSway(float DeltaTime)
{
	// Early out if disabled or no settings
	if (!MovementSettings || !MovementSettings->bEnableWeaponRunSway)
	{
		CurrentRunSwayRotation = FRotator::ZeroRotator;
		CurrentRunSwayPosition = FVector::ZeroVector;
		CurrentRunSwayIntensity = 0.0f;
		return;
	}

	// Get current state
	const bool bIsSliding = ApexMovement && ApexMovement->IsSliding();
	const bool bIsWallrunning = ApexMovement && ApexMovement->IsWallRunning();
	const bool bIsMantling = ApexMovement && ApexMovement->bIsMantling;
	const bool bIsCrouching = GetCharacterMovement() && GetCharacterMovement()->IsCrouching();
	const bool bIsOnGround = GetCharacterMovement() && GetCharacterMovement()->IsMovingOnGround();
	const bool bIsFalling = GetCharacterMovement() && GetCharacterMovement()->IsFalling();
	const bool bIsSprinting = ApexMovement && ApexMovement->IsSprinting();

	// Calculate current horizontal speed
	FVector Velocity = GetCharacterMovement() ? GetCharacterMovement()->Velocity : FVector::ZeroVector;
	float HorizontalSpeed = FVector(Velocity.X, Velocity.Y, 0.0f).Size();

	// Determine target intensity based on movement state
	float TargetIntensity = 0.0f;

	// Only apply sway when running on ground (not sliding, crouching, wallrunning, etc.)
	if (bIsOnGround && !bIsSliding && !bIsCrouching && !bIsMantling && HorizontalSpeed > MovementSettings->WeaponRunSwayMinSpeed)
	{
		// Calculate intensity based on speed
		float SpeedAlpha = FMath::Clamp(
			(HorizontalSpeed - MovementSettings->WeaponRunSwayMinSpeed) /
			(MovementSettings->WeaponRunSwayMaxSpeedRef - MovementSettings->WeaponRunSwayMinSpeed),
			0.0f, 1.0f
		);

		TargetIntensity = SpeedAlpha;

		// Apply sprint multiplier
		if (bIsSprinting)
		{
			TargetIntensity *= MovementSettings->WeaponRunSwaySprintMultiplier;
		}

		TargetIntensity = FMath::Clamp(TargetIntensity, 0.0f, 1.0f);
	}

	// Interpolate intensity for smooth transitions
	CurrentRunSwayIntensity = FMath::FInterpTo(
		CurrentRunSwayIntensity,
		TargetIntensity,
		DeltaTime,
		MovementSettings->WeaponRunSwayInterpSpeed
	);

	// Calculate traveled distance this frame
	FVector CurrentLocation = GetActorLocation();
	float FrameDistance = 0.0f;

	if (bHasValidPreviousLocation && TargetIntensity > 0.0f)
	{
		FVector Delta = CurrentLocation - PreviousFrameLocation;
		Delta.Z = 0.0f; // Only horizontal distance
		FrameDistance = Delta.Size();
	}

	PreviousFrameLocation = CurrentLocation;
	bHasValidPreviousLocation = true;

	// Calculate step distance with sprint modifier
	float StepDistance = MovementSettings->WeaponRunSwayStepDistance;
	if (bIsSprinting)
	{
		StepDistance /= MovementSettings->WeaponRunSwaySprintFrequencyMultiplier;
	}

	// Accumulate distance and update phase
	if (CurrentRunSwayIntensity > 0.01f)
	{
		RunSwayAccumulatedDistance += FrameDistance;

		// Wrap accumulated distance to one step cycle
		if (RunSwayAccumulatedDistance >= StepDistance)
		{
			RunSwayAccumulatedDistance = FMath::Fmod(RunSwayAccumulatedDistance, StepDistance);
		}

		// Calculate phase (0-1)
		CurrentRunSwayPhase = RunSwayAccumulatedDistance / StepDistance;
	}
	else
	{
		// Smoothly reset phase when not moving
		CurrentRunSwayPhase = FMath::FInterpTo(CurrentRunSwayPhase, 0.0f, DeltaTime, 4.0f);
		RunSwayAccumulatedDistance = CurrentRunSwayPhase * StepDistance;
	}

	// Sample curves if available
	FRotator TargetRotation = FRotator::ZeroRotator;
	FVector TargetPosition = FVector::ZeroVector;

	if (MovementSettings->WeaponRunSwayCurve)
	{
		// Sample the curve at current phase
		FVector CurveValue = MovementSettings->WeaponRunSwayCurve->GetVectorValue(CurrentRunSwayPhase);

		// Apply to rotation with intensity and amounts
		TargetRotation.Roll = CurveValue.X * MovementSettings->WeaponRunSwayRollAmount * CurrentRunSwayIntensity;
		TargetRotation.Pitch = CurveValue.Y * MovementSettings->WeaponRunSwayPitchAmount * CurrentRunSwayIntensity;
		TargetRotation.Yaw = CurveValue.Z * MovementSettings->WeaponRunSwayYawAmount * CurrentRunSwayIntensity;
	}
	else
	{
		// Fallback: procedural "figure-8" pattern using sin/cos
		// This creates a Titanfall-style pattern with step accents
		float Phase2Pi = CurrentRunSwayPhase * 2.0f * PI;

		// Roll: full cycle per step (left-right)
		float RollValue = FMath::Sin(Phase2Pi);

		// Pitch: two cycles per step with accent at step points
		// Using sin^2 creates a "bounce" at 0 and 0.5 phase
		float PitchBase = FMath::Sin(Phase2Pi * 2.0f);
		// Add accent at step points (0 and 0.5)
		float StepAccent = FMath::Pow(FMath::Abs(FMath::Cos(Phase2Pi)), 3.0f);
		float PitchValue = PitchBase * 0.7f - StepAccent * 0.5f;

		// Small yaw oscillation
		float YawValue = FMath::Sin(Phase2Pi) * 0.3f;

		TargetRotation.Roll = RollValue * MovementSettings->WeaponRunSwayRollAmount * CurrentRunSwayIntensity;
		TargetRotation.Pitch = PitchValue * MovementSettings->WeaponRunSwayPitchAmount * CurrentRunSwayIntensity;
		TargetRotation.Yaw = YawValue * MovementSettings->WeaponRunSwayYawAmount * CurrentRunSwayIntensity;
	}

	// Sample position curve if available
	if (MovementSettings->WeaponRunSwayPositionCurve)
	{
		FVector PosCurveValue = MovementSettings->WeaponRunSwayPositionCurve->GetVectorValue(CurrentRunSwayPhase);
		TargetPosition = PosCurveValue * MovementSettings->WeaponRunSwayPositionAmount * CurrentRunSwayIntensity;
	}
	else if (CurrentRunSwayIntensity > 0.01f)
	{
		// Fallback: small position offset matching rotation
		float Phase2Pi = CurrentRunSwayPhase * 2.0f * PI;
		TargetPosition.Y = FMath::Sin(Phase2Pi) * MovementSettings->WeaponRunSwayPositionAmount * CurrentRunSwayIntensity * 0.5f;
		TargetPosition.Z = -FMath::Abs(FMath::Sin(Phase2Pi * 2.0f)) * MovementSettings->WeaponRunSwayPositionAmount * CurrentRunSwayIntensity * 0.3f;
	}

	// Apply final values (already interpolated via intensity)
	CurrentRunSwayRotation = TargetRotation;
	CurrentRunSwayPosition = TargetPosition;
}

// ==================== Aim Offset for AnimBP ====================

void APolarityCharacter::UpdateAnimInstanceAimOffset(float DeltaTime)
{
	if (!MovementSettings || !MovementSettings->bEnableRunAimOffset)
	{
		// Reset to zero when disabled
		if (!CurrentAimOffset.IsNearlyZero())
		{
			CurrentAimOffset = FMath::VInterpTo(CurrentAimOffset, FVector::ZeroVector, DeltaTime, 10.0f);
			SetAnimInstanceAimOffset(CurrentAimOffset);
		}
		return;
	}

	// Get current state
	const bool bIsSliding = ApexMovement && ApexMovement->IsSliding();
	const bool bIsWallrunning = ApexMovement && ApexMovement->IsWallRunning();
	const bool bIsMantling = ApexMovement && ApexMovement->bIsMantling;
	const bool bIsCrouching = GetCharacterMovement() && GetCharacterMovement()->IsCrouching();
	const bool bIsOnGround = GetCharacterMovement() && GetCharacterMovement()->IsMovingOnGround();
	const bool bIsSprinting = ApexMovement && ApexMovement->IsSprinting();

	// Calculate horizontal speed
	FVector Velocity = GetCharacterMovement() ? GetCharacterMovement()->Velocity : FVector::ZeroVector;
	float HorizontalSpeed = FVector(Velocity.X, Velocity.Y, 0.0f).Size();

	// Determine target aim offset
	TargetAimOffset = FVector::ZeroVector;

	// Only apply when running on ground
	if (bIsOnGround && !bIsSliding && !bIsCrouching && !bIsMantling && !bIsWallrunning)
	{
		if (HorizontalSpeed > MovementSettings->AimOffsetMinSpeed)
		{
			if (bIsSprinting)
			{
				TargetAimOffset = MovementSettings->SprintAimOffset;
			}
			else
			{
				TargetAimOffset = MovementSettings->RunAimOffset;
			}
		}
	}

	// Interpolate
	CurrentAimOffset = FMath::VInterpTo(
		CurrentAimOffset,
		TargetAimOffset,
		DeltaTime,
		MovementSettings->AimOffsetInterpSpeed
	);

	// Send to AnimInstance
	SetAnimInstanceAimOffset(CurrentAimOffset);
}

void APolarityCharacter::SetAnimInstanceAimOffset(const FVector& Offset)
{
	if (!FirstPersonMesh)
	{
		return;
	}

	UAnimInstance* AnimInstance = FirstPersonMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		return;
	}

	// Find and set the AimOffset property via reflection
	static FName AimOffsetName(TEXT("AimOffset"));
	FProperty* Property = AnimInstance->GetClass()->FindPropertyByName(AimOffsetName);

	if (Property)
	{
		FStructProperty* StructProp = CastField<FStructProperty>(Property);
		if (StructProp && StructProp->Struct == TBaseStructure<FVector>::Get())
		{
			void* ValuePtr = StructProp->ContainerPtrToValuePtr<void>(AnimInstance);
			if (ValuePtr)
			{
				*static_cast<FVector*>(ValuePtr) = Offset;
			}
		}
	}
}