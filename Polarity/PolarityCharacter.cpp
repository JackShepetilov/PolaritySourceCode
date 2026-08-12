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

	// Camera is created FIRST — the first person mesh parents to it (see below).
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
	FirstPersonCameraComponent->SetRelativeRotation(FRotator::ZeroRotator);
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	// First person FOV stays ENABLED, but is mirrored onto the world FOV every frame by
	// UCameraShakeComponent::ApplyToCamera (the last writer of FieldOfView in the frame).
	// Note this is NOT the same as clearing the flag: with the flag off the engine substitutes the
	// FOV of the camera COMPONENT, which is not necessarily the FOV the frame is rendered with.
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	// The FP mesh hangs off the CAMERA, not off the body mesh. The hands are modelled without a
	// body, so they simply ride the view instead of being aimed at it by a Control Rig. Every
	// procedural offset below (crouch, wallrun, sway, recoil, ADS) is therefore in camera space.
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(FirstPersonCameraComponent);
	FirstPersonMesh->SetRelativeLocation(FirstPersonMeshCameraOffset);
	FirstPersonMesh->SetRelativeRotation(FirstPersonMeshCameraRotation);
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	// Force full per-frame pose & bone refresh so weapon/attachment world transforms can never
	// lag behind the hand animation (default tick option only refreshes bones during montages).
	FirstPersonMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	FirstPersonMesh->bEnableUpdateRateOptimizations = false;

	CameraShakeComponent = CreateDefaultSubobject<UCameraShakeComponent>(TEXT("Camera Shake"));

	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;
	GetCapsuleComponent()->SetCapsuleSize(34.0f, 88.0f);

	GetCharacterMovement()->BrakingDecelerationFalling = 0.0f;
	GetCharacterMovement()->AirControl = 0.0f; // Disabled - custom ApplyAirStrafe() handles all air movement
	// GravityScale is set from MovementSettings in ApexMovementComponent::InitializeComponent()
}

void APolarityCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (ApexMovement && MovementSettings)
	{
		ApexMovement->MovementSettings = MovementSettings;
	}

	// The body must always be visible to everybody except its own owner: in coop a teammate with a
	// hidden body reads as a weapon floating through the air. bOwnerNoSee in the constructor does
	// the "not to yourself" half, but a Blueprint can still switch the mesh off entirely, and
	// Visible and Hidden In Game are two independent flags, so clearing one is not enough. Single
	// player never noticed because nobody was ever looking at this mesh.
	// Propagation is deliberately OFF: MeleeWeaponFPMesh hangs off this mesh and is meant to stay
	// hidden until a melee weapon is equipped.
	if (USkeletalMeshComponent* BodyMesh = GetMesh())
	{
		BodyMesh->SetOwnerNoSee(true);
		BodyMesh->SetHiddenInGame(false, /*bPropagateToChildren=*/ false);
		BodyMesh->SetVisibility(true, /*bPropagateToChildren=*/ false);
	}

	// Enforce the camera parenting at runtime. Changing SetupAttachment in the constructor does NOT
	// update content that was already saved against the old hierarchy: the Blueprint keeps an
	// overridden template for this inherited component, and spawned instances get AttachParent =
	// body mesh from it even though the CDO correctly reports the camera. Verified in PIE.
	if (FirstPersonMesh && FirstPersonCameraComponent &&
		FirstPersonMesh->GetAttachParent() != FirstPersonCameraComponent)
	{
		UE_LOG(LogTemplateCharacter, Warning,
			TEXT("[FPMESH_DEBUG] FirstPersonMesh was attached to %s, re-attaching to the camera"),
			FirstPersonMesh->GetAttachParent() ? *FirstPersonMesh->GetAttachParent()->GetName() : TEXT("nothing"));

		FirstPersonMesh->AttachToComponent(FirstPersonCameraComponent,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}

	// Rest pose of FirstPersonMesh under the camera. Taken from the dedicated properties rather
	// than from the component's current relative transform: the transform is rewritten every tick
	// by the pose pipeline, so reading it back would be a moving target.
	FirstPersonMeshBaseLocation = FirstPersonMeshCameraOffset;
	FirstPersonMeshBaseRotation = FirstPersonMeshCameraRotation;
	if (FirstPersonMesh)
	{
		FirstPersonMesh->SetRelativeLocation(FirstPersonMeshBaseLocation);
		FirstPersonMesh->SetRelativeRotation(FirstPersonMeshBaseRotation);
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

		if (DashAction)
		{
			EnhancedInputComponent->BindAction(DashAction, ETriggerEvent::Started, this, &APolarityCharacter::DashPressed);
		}

		if (ToggleChargeAction)
		{
			EnhancedInputComponent->BindAction(ToggleChargeAction, ETriggerEvent::Started, this, &APolarityCharacter::DoToggleChargePressed);
			EnhancedInputComponent->BindAction(ToggleChargeAction, ETriggerEvent::Completed, this, &APolarityCharacter::DoToggleChargeReleased);
		}

		if (ChannelAction)
		{
			EnhancedInputComponent->BindAction(ChannelAction, ETriggerEvent::Started, this, &APolarityCharacter::DoChannelPressed);
			EnhancedInputComponent->BindAction(ChannelAction, ETriggerEvent::Completed, this, &APolarityCharacter::DoChannelReleased);
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

bool APolarityCharacter::CanJumpInternal_Implementation() const
{
	// The engine's default gate assumes engine jump rules (grounded, JumpCurrentCount, crouch).
	// This game's rules live entirely in UApexMovementComponent::DoJump, which tracks its own jump
	// count and handles wall running and slide hops. Letting the default gate run would block those
	// before DoJump ever gets a say, and it is DoJump that returns false when a jump is not
	// available, so nothing is lost by being permissive here.
	return ApexMovement ? true : Super::CanJumpInternal_Implementation();
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
		ApexMovement->TryCrouchSlide();
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

void APolarityCharacter::DashPressed(const FInputActionValue& Value)
{
	if (!ApexMovement)
	{
		return;
	}

	const bool bDashed = ApexMovement->IsMovingOnGround()
		? ApexMovement->TryGroundDash()
		: ApexMovement->TryAirDash();

	if (bDashed && CameraShakeComponent)
	{
		// The existing dash FOV kick is suitable for both variants until Ground Dash needs bespoke tuning.
		CameraShakeComponent->TriggerAirDash();
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

void APolarityCharacter::DoToggleChargePressed()
{
	UChargeAnimationComponent* ChargeAnim = FindComponentByClass<UChargeAnimationComponent>();
	if (ChargeAnim)
	{
		ChargeAnim->OnChargeButtonPressed();
	}
}

void APolarityCharacter::DoToggleChargeReleased()
{
	UChargeAnimationComponent* ChargeAnim = FindComponentByClass<UChargeAnimationComponent>();
	if (ChargeAnim)
	{
		ChargeAnim->OnChargeButtonReleased();
	}
}

void APolarityCharacter::DoChannelPressed()
{
	UChargeAnimationComponent* ChargeAnim = FindComponentByClass<UChargeAnimationComponent>();
	if (ChargeAnim)
	{
		ChargeAnim->OnChannelButtonPressed();
	}
}

void APolarityCharacter::DoChannelReleased()
{
	UChargeAnimationComponent* ChargeAnim = FindComponentByClass<UChargeAnimationComponent>();
	if (ChargeAnim)
	{
		ChargeAnim->OnChannelButtonReleased();
	}
}

// ==================== First Person View ====================

void APolarityCharacter::UpdateFirstPersonView(float DeltaTime)
{
	if (!FirstPersonMesh || !MovementSettings)
	{
		return;
	}

	// Run aim offset is a pose layer now, so it has to be interpolated before the pose is built.
	UpdateRunAimOffset(DeltaTime);

	// Single writer for the FP mesh transform: every system contributes a layer to Location /
	// Rotation, and the accumulated result is applied exactly once, at the end.
	FVector  Location = FirstPersonMeshBaseLocation;
	FRotator Rotation = FirstPersonMeshBaseRotation;

	AccumulateFirstPersonPose(DeltaTime, Location, Rotation);

	FirstPersonMesh->SetRelativeLocation(Location);
	FirstPersonMesh->SetRelativeRotation(Rotation);

	ApplyCameraManagerRoll();
}

void APolarityCharacter::AccumulateFirstPersonPose(float DeltaTime, FVector& Location, FRotator& Rotation)
{
	if (!MovementSettings)
	{
		return;
	}

	// ==================== Movement State Detection ====================

	bool bIsSliding = false;
	bool bIsCrouching = false;
	bool bIsWallrunning = false;

	if (ApexMovement)
	{
		bIsSliding = ApexMovement->IsSliding();
		bIsCrouching = ApexMovement->IsCrouching();
		bIsWallrunning = ApexMovement->IsWallRunning();
	}

	// ==================== Crouch/Slide Tilt & Offset (Single Progress) ====================
	// Use ONE interpolated progress for both offset and rotation.
	// This ensures straight-line movement from start to end position.

	float TargetProgress = 0.0f;

	if (MovementSettings->bEnableWeaponTilt && ApexMovement)
	{
		// Check if crouch input is held (works in air too)
		bool bCrouchInputHeld = ApexMovement->bWantsSlideOnLand;
		bool bIsInAir = !ApexMovement->IsMovingOnGround();

		if (bIsSliding)
		{
			// Save target values when entering slide
			SavedCrouchSlideTilt.Roll = MovementSettings->SlideWeaponTiltRoll;
			SavedCrouchSlideTilt.Pitch = MovementSettings->SlideWeaponTiltPitch;
			SavedCrouchSlideOffset = MovementSettings->SlideCameraOffset;
			TargetProgress = 1.0f;
		}
		else if (bIsCrouching || (bCrouchInputHeld))
		{
			// Save target values when crouching OR when crouch button held in air
			SavedCrouchSlideTilt.Roll = MovementSettings->CrouchWeaponTiltRoll;
			SavedCrouchSlideTilt.Pitch = MovementSettings->CrouchWeaponTiltPitch;
			SavedCrouchSlideOffset = MovementSettings->CrouchCameraOffset;
			TargetProgress = 1.0f;
		}
		// When not crouching/sliding, TargetProgress = 0 but we keep saved values for exit transition
	}

	// Interpolate single progress value
	CrouchSlideProgress = FMath::FInterpTo(
		CrouchSlideProgress,
		TargetProgress,
		DeltaTime,
		MovementSettings->WeaponTiltInterpSpeed
	);

	// Apply progress to saved targets - this creates straight-line motion in both directions
	CurrentCrouchOffset = SavedCrouchSlideOffset * CrouchSlideProgress;
	FRotator CrouchSlideTilt = SavedCrouchSlideTilt * CrouchSlideProgress;

	// Clear saved values when fully returned to standing
	if (CrouchSlideProgress < KINDA_SMALL_NUMBER)
	{
		SavedCrouchSlideOffset = FVector::ZeroVector;
		SavedCrouchSlideTilt = FRotator::ZeroRotator;
	}

	// ==================== Wallrun Tilt ====================

	FRotator WallrunMeshTilt = FRotator::ZeroRotator;
	if (bIsWallrunning && ApexMovement && MovementSettings->bEnableWeaponTilt)
	{
		// Use the pre-calculated mesh tilt from ApexMovement
		WallrunMeshTilt.Roll = ApexMovement->CurrentWallRunMeshRoll;
		WallrunMeshTilt.Pitch = ApexMovement->CurrentWallRunMeshPitch;

		UE_LOG(LogTemplateCharacter, Warning, TEXT("WallRun Mesh: Side=%s, MeshPitch=%.2f, CameraRoll=%.2f"),
			ApexMovement->WallRunSide == EWallSide::Left ? TEXT("Left") : TEXT("Right"),
			ApexMovement->CurrentWallRunMeshPitch,
			ApexMovement->CurrentWallRunCameraRoll);
	}

	// Wallrun camera offset - use the pre-calculated offset from ApexMovement
	if (bIsWallrunning && ApexMovement)
	{
		TargetWallrunOffset = ApexMovement->CurrentWallRunCameraOffset;
	}
	else
	{
		TargetWallrunOffset = FVector::ZeroVector;
	}

	// Combine all tilts: crouch/slide + wallrun
	CurrentWeaponTilt = CrouchSlideTilt + WallrunMeshTilt;

	// ==================== Camera Roll Follow ====================
	// APolarityCameraManager applies its roll to the final POV rotation, NOT to the camera
	// component, so a camera-parented mesh does not inherit it. Both rolls are therefore mirrored
	// onto the mesh by hand, each with its own follow factor:
	//   shake roll   — 1.0 by default (the mesh always shook with it)
	//   wallrun roll — 0.0 by default (deliberately kept off the mesh so the barrel does not
	//                  clip into the wall the player is running on)
	if (CameraShakeComponent)
	{
		CurrentWeaponTilt.Roll += CameraShakeComponent->GetCameraRotationOffset().Roll * ShakeRollFollowAlpha;
	}
	if (ApexMovement && WallrunRollFollowAlpha > 0.0f)
	{
		CurrentWeaponTilt.Roll += ApexMovement->CurrentWallRunCameraRoll * WallrunRollFollowAlpha;
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

	// ==================== Accumulate ====================

	Location += CurrentCrouchOffset;
	Location += (CurrentADSOffset + CurrentWallrunOffset);
	Location += CurrentRunSwayPosition;
	// Run aim offset: "where the weapon points while running". Used to be fed into the Control
	// Rig's aim target; with the mesh riding the camera it is a plain camera-space offset.
	Location += CurrentAimOffset;

	// ==================== Tilt & sway, pivoted between the shoulders ====================
	// SetRelativeRotation spins the component around ITS OWN origin, and that origin is the root
	// of the skeleton — down at the feet, measured at ~153 cm below the eye. Adding tilt straight
	// into Rotation therefore swung the weapon along a 154 cm arc, about 2.7 cm of travel per
	// degree, which reads as the gun sliding away rather than nodding.
	// Instead, rotate around a fixed point in CAMERA space. Camera space (not mesh space) is what
	// keeps this free of feedback: the pivot never depends on where the accumulated pose put the
	// mesh this frame. Measured in game, the midpoint between upperarm_l and upperarm_r sits at
	// roughly (25, 5, -22), which is the value below.
	const FVector ShoulderPivot(25.0f, 5.0f, -22.0f);

	const FQuat PivotedRotation = (CurrentWeaponTilt + CurrentRunSwayRotation).Quaternion();
	Location = ShoulderPivot + PivotedRotation.RotateVector(Location - ShoulderPivot);
	Rotation = (PivotedRotation * Rotation.Quaternion()).Rotator();
}

void APolarityCharacter::ApplyCameraManagerRoll()
{
	// Wallrun roll is already interpolated in ApexMovement, use it directly.
	float WallrunCameraRoll = 0.0f;
	if (ApexMovement)
	{
		WallrunCameraRoll = ApexMovement->CurrentWallRunCameraRoll;
	}

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

// ==================== Run Aim Offset ====================

void APolarityCharacter::UpdateRunAimOffset(float DeltaTime)
{
	if (!MovementSettings || !MovementSettings->bEnableRunAimOffset)
	{
		// Reset to zero when disabled
		if (!CurrentAimOffset.IsNearlyZero())
		{
			CurrentAimOffset = FMath::VInterpTo(CurrentAimOffset, FVector::ZeroVector, DeltaTime, 10.0f);
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
}
