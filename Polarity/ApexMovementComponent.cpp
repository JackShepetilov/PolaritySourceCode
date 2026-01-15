// ApexMovementComponent.cpp
// Titanfall 2 / Apex Legends style movement implementation

#include "ApexMovementComponent.h"
#include "MovementSettings.h"
#include "VelocityModifier.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSlide, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogWallRun, Log, All);

UApexMovementComponent::UApexMovementComponent()
{
	NavAgentProps.bCanCrouch = true;
	bCanWalkOffLedgesWhenCrouching = true;
	SetCrouchedHalfHeight(50.0f);

	AirControl = 0.3f;
	JumpZVelocity = 500.0f;
	GravityScale = 1.5f;
	MaxWalkSpeed = 600.0f;
	MaxWalkSpeedCrouched = 300.0f;
	BrakingDecelerationWalking = 2048.0f;
	GroundFriction = 6.0f;
}

void UApexMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();

	// Cache standing capsule height for smooth crouch
	// Use GetCharacterOwner() which is guaranteed to work after component registration
	ACharacter* Owner = GetCharacterOwner();
	if (Owner)
	{
		UCapsuleComponent* Capsule = Owner->GetCapsuleComponent();
		if (Capsule)
		{
			StandingCapsuleHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
			TargetCapsuleHalfHeight = StandingCapsuleHalfHeight;
		}
	}

	if (MovementSettings)
	{
		// Fallback: use MovementSettings if capsule wasn't available
		if (StandingCapsuleHalfHeight <= 0.0f)
		{
			StandingCapsuleHalfHeight = MovementSettings->StandingCapsuleHalfHeight;
			TargetCapsuleHalfHeight = StandingCapsuleHalfHeight;
		}

		// Also set CrouchedHalfHeight from settings
		SetCrouchedHalfHeight(MovementSettings->CrouchingCapsuleHalfHeight);

		JumpZVelocity = MovementSettings->JumpZVelocity;
		MaxWalkSpeed = MovementSettings->WalkSpeed;
		MaxWalkSpeedCrouched = MovementSettings->CrouchSpeed;
		GroundFriction = MovementSettings->GroundFriction;
		BrakingDecelerationWalking = MovementSettings->BrakingDeceleration;
		AirControl = MovementSettings->AirControl;

		DefaultGroundFriction = MovementSettings->GroundFriction;
		DefaultBrakingDeceleration = MovementSettings->BrakingDeceleration;

		OwnerCharacter = Cast<ACharacter>(GetOwner());
		if (OwnerCharacter)
		{
			OwnerController = Cast<APlayerController>(OwnerCharacter->GetController());
		}
	}
	else
	{
		DefaultGroundFriction = GroundFriction;
		DefaultBrakingDeceleration = BrakingDecelerationWalking;
	}
}

void UApexMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// Update cooldowns
	if (SlideCooldownRemaining > 0.0f)
	{
		SlideCooldownRemaining -= DeltaTime;
	}
	if (SlideBoostCooldownRemaining > 0.0f)
	{
		SlideBoostCooldownRemaining -= DeltaTime;
	}
	if (AirDashCooldownRemaining > 0.0f)
	{
		AirDashCooldownRemaining -= DeltaTime;
	}
	if (WallRunSameWallCooldown > 0.0f)
	{
		WallRunSameWallCooldown -= DeltaTime;
	}
	if (WallBounceCooldownRemaining > 0.0f)
	{
		WallBounceCooldownRemaining -= DeltaTime;
	}

	// Decrease slide fatigue over time when not sliding
	if (!bIsSliding && SlideFatigueCounter > 0)
	{
		SlideFatigueDecayTimer += DeltaTime;
		if (SlideFatigueDecayTimer >= 1.0f)
		{
			SlideFatigueCounter--;
			SlideFatigueDecayTimer = 0.0f;
		}
	}

	// Smooth crouch - interpolate capsule height
	UpdateCapsuleHeight(DeltaTime);

	// Pre-tick: Update mechanics that need to run BEFORE physics
	if (bIsMantling)
	{
		UpdateMantle(DeltaTime);
	}
	else if (bIsWallRunning)
	{
		UpdateWallRun(DeltaTime);
	}
	else if (bIsAirDashing)
	{
		UpdateAirDash(DeltaTime);
	}
	else if (IsFalling() && !bIsSliding)
	{
		// Check wall bounce FIRST - only if forward is held
		if (IsForwardHeld())
		{
			CheckForWallBounce();
		}

		// If not bounced, check for wall run
		if (!bIsWallRunning)
		{
			CheckForWallRun();
		}

		ApplyAirStrafe(DeltaTime);
	}

	// Jump hold (variable jump height)
	if (bJumpHeld && IsFalling())
	{
		UpdateJumpHold(DeltaTime);
	}

	// Update camera tilt for wallrun
	UpdateWallRunCameraTilt(DeltaTime);

	// Air dash decay near ground
	if (AirDashDecayTimeRemaining > 0.0f && IsFalling())
	{
		UpdateAirDashDecay(DeltaTime);
	}
	else if (!IsFalling())
	{
		AirDashDecayTimeRemaining = 0.0f;
	}

	// Apply external forces BEFORE parent tick
	ApplyEMFForces(DeltaTime);
	ApplyVelocityModifiers(DeltaTime);
	OnPreVelocityUpdate.Broadcast(DeltaTime, Velocity);

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// POST-TICK: Slide deceleration AFTER physics
	if (bIsSliding)
	{
		UpdateSlide(DeltaTime);
	}

	UpdateMovementState();
}

float UApexMovementComponent::GetMaxSpeed() const
{
	if (!MovementSettings)
	{
		return Super::GetMaxSpeed();
	}

	if (bIsSliding || bIsWallRunning)
	{
		// No speed cap - momentum based
		return 10000.0f;
	}

	if (IsCrouching())
	{
		return MovementSettings->CrouchSpeed;
	}

	if (IsSprinting())
	{
		return MovementSettings->SprintSpeed;
	}

	switch (MovementMode)
	{
	case MOVE_Walking:
	case MOVE_NavWalking:
		return MovementSettings->WalkSpeed;
	case MOVE_Falling:
		return MovementSettings->SprintSpeed;
	default:
		return Super::GetMaxSpeed();
	}
}

float UApexMovementComponent::GetMaxAcceleration() const
{
	// No player acceleration during slide or wallrun - momentum only
	if (bIsSliding || bIsWallRunning)
	{
		return 0.0f;
	}

	if (!MovementSettings)
	{
		return Super::GetMaxAcceleration();
	}

	return IsFalling() ? MovementSettings->AirAcceleration : MovementSettings->GroundAcceleration;
}

void UApexMovementComponent::ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations)
{
	LastFallVelocity = FMath::Abs(Velocity.Z);

	const FVector PreLandHorizontalVelocity = FVector(Velocity.X, Velocity.Y, 0.0f);
	const float PreLandSpeed = PreLandHorizontalVelocity.Size();

	Super::ProcessLanded(Hit, remainingTime, Iterations);
	CurrentJumpCount = 0;
	LastWallRunEndReason = EWallRunEndReason::None;
	ResetAirAbilities();

	if (bIsWallRunning)
	{
		EndWallRun(EWallRunEndReason::LostWall);
	}

	if (bWantsSlideOnLand && PreLandSpeed > 0.0f)
	{
		Velocity.X = PreLandHorizontalVelocity.X;
		Velocity.Y = PreLandHorizontalVelocity.Y;

		StartSlideFromAir(LastFallVelocity);
		//bWantsSlideOnLand = false;
	}

	OnLanded_Movement.Broadcast(Hit);
}

bool UApexMovementComponent::TryJump()
{
	bool JumpResult = DoJump(false);

	if (JumpResult)
	{
		PlayCameraShake(JumpCameraShake);
	}

	return JumpResult;
}

bool UApexMovementComponent::DoJump(bool bReplayingMoves)
{
	if (!MovementSettings)
	{
		return Super::DoJump(bReplayingMoves);
	}

	const int32 MaxJumps = MovementSettings->MaxJumpCount;

	// Wall jump - player pushed off wall, NO double jump allowed after
	if (bIsWallRunning)
	{
		FVector JumpVelocity = WallRunNormal * MovementSettings->WallJumpSideForce;
		JumpVelocity.Z = MovementSettings->WallJumpUpForce;

		// Add forward momentum from wall run
		JumpVelocity += WallRunDirection * (WallRunEntryVelocity.Size2D() * 0.5f);

		EndWallRun(EWallRunEndReason::JumpedOff);
		Velocity = JumpVelocity;
		SetMovementMode(MOVE_Falling);

		// After wall jump, player cannot double jump (set to max jumps)
		CurrentJumpCount = MaxJumps;

		// Trigger Blueprint event
		if (CharacterOwner)
		{
			CharacterOwner->OnJumped();
		}

		return true;
	}

	// Check if we're falling after wallrun ended by time expiration
	// In this case, player gets ONE jump
	if (IsFalling() && LastWallRunEndReason == EWallRunEndReason::TimeExpired && CurrentJumpCount < MaxJumps)
	{
		// Allow the jump but consume all remaining jumps
		Velocity.Z = MovementSettings->JumpZVelocity;
		CurrentJumpCount = MaxJumps; // Consume all jumps
		LastWallRunEndReason = EWallRunEndReason::None; // Reset so this only works once
		SetMovementMode(MOVE_Falling);

		// Trigger Blueprint event
		if (CharacterOwner)
		{
			CharacterOwner->OnJumped();
		}

		return true;
	}

	if (CurrentJumpCount >= MaxJumps)
	{
		return false;
	}

	// Slide jump (slidehop)
	if (bIsSliding)
	{
		FVector HorizontalVelocity = FVector(Velocity.X, Velocity.Y, 0.0f);
		float CurrentSpeed = HorizontalVelocity.Size();

		EndSlide();

		Velocity.X = HorizontalVelocity.X;
		Velocity.Y = HorizontalVelocity.Y;
		Velocity.Z = MovementSettings->SlidehopJumpZVelocity;

		if (CurrentSpeed > 0.0f && SlideFatigueCounter < 5)
		{
			float FatigueMultiplier = 1.0f - (SlideFatigueCounter * 0.15f);
			FVector BoostDir = HorizontalVelocity.GetSafeNormal();
			Velocity += BoostDir * MovementSettings->SlideJumpBoost * FMath::Max(0.2f, FatigueMultiplier);
		}

		SlideFatigueCounter = FMath::Min(SlideFatigueCounter + 1, 5);
		SlideFatigueDecayTimer = 0.0f;

		// Prevent double boost on landing
		SlideBoostCooldownRemaining = MovementSettings->SlideboostCooldown;

		CurrentJumpCount++;
		SetMovementMode(MOVE_Falling);
		bJumpHeld = true;
		JumpHoldTimeRemaining = MovementSettings->JumpHoldTime;

		// Trigger Blueprint event
		if (CharacterOwner)
		{
			CharacterOwner->OnJumped();
		}

		return true;
	}

	// Normal/Double jump
	if (IsMovingOnGround() || (IsFalling() && CurrentJumpCount < MaxJumps))
	{
		Velocity.Z = MovementSettings->JumpZVelocity;
		CurrentJumpCount++;
		SetMovementMode(MOVE_Falling);

		if (CurrentJumpCount == 1)
		{
			bJumpHeld = true;
			JumpHoldTimeRemaining = MovementSettings->JumpHoldTime;
		}

		// Trigger Blueprint event
		if (CharacterOwner)
		{
			CharacterOwner->OnJumped();
		}

		return true;
	}

	return false;
}

// ==================== Input ====================

void UApexMovementComponent::StartSprint()
{
	bWantsToSprint = true;
}

void UApexMovementComponent::StopSprint()
{
	bWantsToSprint = false;
}

void UApexMovementComponent::TryCrouchSlide()
{
	if (IsFalling() && !bIsWallRunning)
	{
		bWantsSlideOnLand = true;

		if (CanAirDash())
		{
			TryAirDash();
		}
		return;
	}

	if (CanSlide())
	{
		StartSlide();
	}
	else
	{
		StartCrouching();
	}
}

void UApexMovementComponent::StopCrouchSlide()
{
	bWantsSlideOnLand = false;

	if (bIsSliding)
	{
		EndSlide();
	}

	StopCrouching();
}

// ==================== Slide ====================

bool UApexMovementComponent::CanSlide() const
{
	if (bIsSliding || bIsMantling || bIsWallRunning || !IsMovingOnGround())
	{
		return false;
	}

	if (SlideCooldownRemaining > 0.0f)
	{
		return false;
	}

	const float MinStartSpeed = MovementSettings ? MovementSettings->SlideMinStartSpeed : 400.0f;
	return Velocity.Size2D() >= MinStartSpeed;
}

void UApexMovementComponent::StartSlide()
{
	if (!CanSlide())
	{
		return;
	}

	bIsSliding = true;
	SlideDuration = 0.0f;
	SlideDirection = Velocity.GetSafeNormal2D();

	GroundFriction = 0.0f;
	BrakingDecelerationWalking = 100.0f;

	if (SlideBoostCooldownRemaining <= 0.0f)
	{
		const float CurrentSpeed = Velocity.Size2D();
		const float MinBoost = MovementSettings->SlideMinSpeedBurst;
		const float MaxBoost = MovementSettings->SlideMaxSpeedBurst;
		const float MinStartSpeed = MovementSettings->SlideMinStartSpeed;

		float SpeedRatio = FMath::Clamp((CurrentSpeed - MinStartSpeed) / 500.0f, 0.0f, 1.0f);
		float BoostAmount = FMath::Lerp(MaxBoost, MinBoost, SpeedRatio);

		Velocity += SlideDirection * BoostAmount;
		SlideBoostCooldownRemaining = MovementSettings->SlideboostCooldown;

		UE_LOG(LogSlide, Log, TEXT("Slide boost: +%.1f (speed was %.1f), slide cooldown = %.1f"), BoostAmount, CurrentSpeed, SlideBoostCooldownRemaining);
	}

	UE_LOG(LogSlide, Warning, TEXT("=== SLIDE STARTED === Speed=%.1f"), Velocity.Size2D());

	StartCrouching();

	OnSlideStarted.Broadcast();
}

void UApexMovementComponent::EndSlide()
{
	if (!bIsSliding)
	{
		return;
	}

	UE_LOG(LogSlide, Log, TEXT("SLIDE ENDED: Duration=%.2f, FinalSpeed=%.1f"), SlideDuration, Velocity.Size2D());

	bIsSliding = false;
	SlideDuration = 0.0f;

	GroundFriction = (DefaultGroundFriction > 0.0f) ? DefaultGroundFriction : 8.0f;
	BrakingDecelerationWalking = (DefaultBrakingDeceleration > 0.0f) ? DefaultBrakingDeceleration : 2048.0f;

	if (MovementSettings && MovementSettings->SlideCooldown > 0.0f)
	{
		SlideCooldownRemaining = MovementSettings->SlideCooldown;
	}
	else
	{
		SlideCooldownRemaining = 0.3f;
	}

	OnSlideEnded.Broadcast();
}

void UApexMovementComponent::StartSlideFromAir(float FallSpeed)
{
	if (bIsSliding || bIsMantling || bIsWallRunning)
	{
		return;
	}

	bIsSliding = true;
	SlideDuration = 0.0f;
	SlideDirection = Velocity.GetSafeNormal2D();

	GroundFriction = MovementSettings->SlideFriction;
	BrakingDecelerationWalking = 100.0f;

	const float CurrentSpeed = Velocity.Size2D();
	const float MinBoost = MovementSettings->SlideMinSpeedBurst;
	const float MaxBoost = MovementSettings->SlideMaxSpeedBurst;
	const float MinStartSpeed = MovementSettings->SlideMinStartSpeed;

	float SpeedRatio = FMath::Clamp((CurrentSpeed - MinStartSpeed) / 500.0f, 0.0f, 1.0f);
	float BaseBoost = FMath::Lerp(MaxBoost, MinBoost, SpeedRatio);
	const float FallBoostMultiplier = FMath::Clamp(FallSpeed / 1000.0f, 0.1f, 0.5f);
	const float FallBoost = CurrentSpeed * FallBoostMultiplier;
	const float TotalBoost = BaseBoost + FallBoost;

	if (SlideDirection.IsNearlyZero())
	{
		SlideDirection = CharacterOwner ? CharacterOwner->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	}

	if (SlideBoostCooldownRemaining <= 0.0f)
	{
		Velocity += SlideDirection * TotalBoost;
		SlideBoostCooldownRemaining = MovementSettings->SlideboostCooldown;
	}
	UE_LOG(LogSlide, Warning, TEXT("=== SLIDE FROM AIR === Speed=%.1f, Boost=%.1f slide cooldown = %.1f"), Velocity.Size2D(), TotalBoost, SlideBoostCooldownRemaining);

	StartCrouching();

	OnSlideStarted.Broadcast();
}

// ==================== Smooth Crouch ====================

void UApexMovementComponent::StartCrouching()
{
	if (!CharacterOwner)
	{
		return;
	}

	bWantsToCrouchSmooth = true;
	bWantsToCrouch = true;
	TargetCapsuleHalfHeight = CrouchedHalfHeight;

	// Set the movement component's crouch flag immediately for IsCrouching() checks
	CharacterOwner->bIsCrouched = true;
}

void UApexMovementComponent::StopCrouching()
{
	UE_LOG(LogSlide, Warning, TEXT("StopCrouching called. CharacterOwner=%s"), CharacterOwner ? TEXT("Valid") : TEXT("NULL"));

	if (!CharacterOwner)
	{
		return;
	}

	// Check if we can stand up before committing
	if (!CanStandUp())
	{
		UE_LOG(LogSlide, Warning, TEXT("StopCrouching: Cannot stand up - blocked by geometry"));
		return;
	}

	UE_LOG(LogSlide, Warning, TEXT("StopCrouching: Setting target height to %.1f"), StandingCapsuleHalfHeight);

	bWantsToCrouchSmooth = false;
	bWantsToCrouch = false;
	TargetCapsuleHalfHeight = StandingCapsuleHalfHeight;

	CharacterOwner->bIsCrouched = false;
}

bool UApexMovementComponent::CanStandUp() const
{
	if (!CharacterOwner)
	{
		return true;
	}

	// If standing height not initialized, allow standing
	if (StandingCapsuleHalfHeight <= 0.0f)
	{
		UE_LOG(LogSlide, Warning, TEXT("CanStandUp: StandingCapsuleHalfHeight not initialized (%.1f)"), StandingCapsuleHalfHeight);
		return true;
	}

	UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	if (!Capsule)
	{
		return true;
	}

	const float CurrentHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	const float HeightDifference = StandingCapsuleHalfHeight - CurrentHalfHeight;

	if (HeightDifference <= 1.0f)
	{
		return true;  // Already at standing height or close enough
	}

	// Calculate where the capsule CENTER would be when standing
	// When standing, the capsule is taller, so center moves UP by HeightDifference
	const FVector CurrentLocation = CharacterOwner->GetActorLocation();
	const FVector StandingLocation = CurrentLocation + FVector(0.0f, 0.0f, HeightDifference);

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(CharacterOwner);

	const float CapsuleRadius = Capsule->GetUnscaledCapsuleRadius();

	// Check if standing capsule would overlap anything at the standing position
	bool bBlocked = GetWorld()->OverlapBlockingTestByChannel(
		StandingLocation,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeCapsule(CapsuleRadius, StandingCapsuleHalfHeight),
		Params
	);

	if (bBlocked)
	{
		UE_LOG(LogSlide, Log, TEXT("CanStandUp: BLOCKED - cannot fit standing capsule (R=%.1f, H=%.1f) at %s"),
			CapsuleRadius, StandingCapsuleHalfHeight, *StandingLocation.ToString());
	}

	return !bBlocked;
}

void UApexMovementComponent::UpdateCapsuleHeight(float DeltaTime)
{
	if (!CharacterOwner || StandingCapsuleHalfHeight <= 0.0f)
	{
		return;
	}

	UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	if (!Capsule)
	{
		return;
	}

	const float CurrentHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();

	// Check if we need to interpolate
	if (FMath::IsNearlyEqual(CurrentHalfHeight, TargetCapsuleHalfHeight, 0.1f))
	{
		// Snap to target if close enough
		if (!FMath::IsNearlyEqual(CurrentHalfHeight, TargetCapsuleHalfHeight, 0.01f))
		{
			Capsule->SetCapsuleHalfHeight(TargetCapsuleHalfHeight);
		}
		return;
	}

	// Interpolate capsule height
	const float NewHalfHeight = FMath::FInterpTo(
		CurrentHalfHeight,
		TargetCapsuleHalfHeight,
		DeltaTime,
		CapsuleInterpSpeed
	);

	// Calculate height delta (positive = growing, negative = shrinking)
	const float HeightDelta = NewHalfHeight - CurrentHalfHeight;

	// When standing up (growing), we need to move the actor UP to keep feet on ground
	// When crouching (shrinking), we keep the actor position (head goes down)
	// Actually for crouch: we want head to stay still, so actor moves DOWN
	// For stand: we want feet to stay still, so actor moves UP

	// The capsule center is at actor location, so:
	// - Shrinking capsule: actor stays, head drops, feet rise -> need to move actor DOWN by HeightDelta
	// - Growing capsule: actor stays, head rises, feet drop -> need to move actor UP by HeightDelta

	// Apply the new capsule height
	Capsule->SetCapsuleHalfHeight(NewHalfHeight);

	// Move actor to keep feet on ground (move by half the delta since capsule grows from center)
	// Negative delta (shrinking) -> move down slightly to compensate
	// Positive delta (growing) -> move up to keep feet planted
	FVector ActorLocation = CharacterOwner->GetActorLocation();
	ActorLocation.Z += HeightDelta;
	CharacterOwner->SetActorLocation(ActorLocation);
}

void UApexMovementComponent::UpdateSlide(float DeltaTime)
{
	const float SlideMinSpeed = MovementSettings->SlideMinSpeed;
	const float SlideFlatDecel = MovementSettings->SlideFlatDeceleration;
	const float SlideUphillDecel = MovementSettings->SlideUphillDeceleration;
	const float SlideDownhillDecel = MovementSettings->SlideSlopeAcceleration;
	const float SlideFloorInfluence = MovementSettings->SlideFloorInfluenceForce;

	if (!IsMovingOnGround())
	{
		UE_LOG(LogSlide, Warning, TEXT("Slide ended: left ground"));
		EndSlide();
		return;
	}

	SlideDuration += DeltaTime;

	const float SpeedBefore = Velocity.Size2D();

	if (SpeedBefore < SlideMinSpeed)
	{
		UE_LOG(LogSlide, Warning, TEXT("Slide ended: speed %.1f < min %.1f"), SpeedBefore, SlideMinSpeed);
		EndSlide();
		return;
	}

	// Slope influence
	const float SlopeAngle = GetSlopeAngle();

	if (CurrentFloor.IsWalkableFloor() && FMath::Abs(SlopeAngle) > 3.0f)
	{
		const FVector FloorNormal = CurrentFloor.HitResult.Normal;
		FVector SlopeDirection = FVector::CrossProduct(
			FloorNormal,
			FVector::CrossProduct(FVector::UpVector, FloorNormal)
		).GetSafeNormal();

		float SlopeInfluence = SlopeAngle / 45.0f;
		Velocity += SlopeDirection * SlideFloorInfluence * SlopeInfluence * DeltaTime;
	}

	// Deceleration
	float BrakingAlpha = FMath::Clamp(SlideDuration / 2.0f, 0.0f, 1.0f);
	float DecelMultiplier = 0.3f + 0.7f * BrakingAlpha;

	FVector HorizontalVel = FVector(Velocity.X, Velocity.Y, 0.0f);
	float HorizontalSpeed = HorizontalVel.Size();

	if (HorizontalSpeed > 0.0f)
	{
		float DecelAmount = SlideFlatDecel * DecelMultiplier * DeltaTime;

		if (SlopeAngle > 5.0f)
		{
			float SlopeMultiplier = SlopeAngle / 45.0f;
			DecelAmount += SlideUphillDecel * SlopeMultiplier * DeltaTime;
		}

		if (SlopeAngle < 5.0f)
		{
			float SlopeMultiplier = SlopeAngle / 45.0f;
			DecelAmount -= SlideDownhillDecel * SlopeMultiplier * DeltaTime;
		}

		float NewSpeed = FMath::Max(HorizontalSpeed - DecelAmount, 0.0f);
		FVector NewDir = HorizontalVel.GetSafeNormal();
		Velocity.X = NewDir.X * NewSpeed;
		Velocity.Y = NewDir.Y * NewSpeed;
	}

	const float SpeedAfter = Velocity.Size2D();

	UE_LOG(LogSlide, Log, TEXT("SLIDE: %.1f -> %.1f (slope=%.1fÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â°)"), SpeedBefore, SpeedAfter, SlopeAngle);

	if (SpeedAfter < SlideMinSpeed)
	{
		UE_LOG(LogSlide, Warning, TEXT("Slide ended: final speed %.1f < min %.1f"), SpeedAfter, SlideMinSpeed);
		EndSlide();
	}
}

float UApexMovementComponent::GetSlopeAngle() const
{
	if (!CurrentFloor.IsWalkableFloor())
	{
		return 0.0f;
	}

	const FVector FloorNormal = CurrentFloor.HitResult.Normal;
	const FVector VelocityDir = Velocity.GetSafeNormal2D();

	if (VelocityDir.IsNearlyZero())
	{
		return 0.0f;
	}

	const FVector SlopeDir = FVector::CrossProduct(FloorNormal, FVector::CrossProduct(VelocityDir, FloorNormal));
	return FMath::RadiansToDegrees(FMath::Asin(SlopeDir.Z));
}

// ==================== Wall Run (Slide-style) ====================

bool UApexMovementComponent::CanWallRun() const
{
	if (!MovementSettings || !MovementSettings->bEnableWallRun)
	{
		return false;
	}

	if (bIsSliding || bIsMantling || bIsWallRunning)
	{
		return false;
	}

	if (!IsFalling())
	{
		return false;
	}

	if (Velocity.Size2D() < MovementSettings->WallRunMinSpeed)
	{
		return false;
	}

	if (!IsAboveGround())
	{
		return false;
	}

	return true;
}

void UApexMovementComponent::CheckForWallRun()
{
	if (!CanWallRun())
	{
		return;
	}

	FHitResult LeftHit, RightHit;
	bool bLeftWall = TraceForWall(EWallSide::Left, LeftHit);
	bool bRightWall = TraceForWall(EWallSide::Right, RightHit);

	FVector MoveDir = Velocity.GetSafeNormal2D();

	if (bLeftWall && bRightWall)
	{
		float LeftDot = FVector::DotProduct(MoveDir, -LeftHit.Normal);
		float RightDot = FVector::DotProduct(MoveDir, -RightHit.Normal);

		if (LeftDot > RightDot)
		{
			StartWallRun(LeftHit, EWallSide::Left);
		}
		else
		{
			StartWallRun(RightHit, EWallSide::Right);
		}
	}
	else if (bLeftWall)
	{
		StartWallRun(LeftHit, EWallSide::Left);
	}
	else if (bRightWall)
	{
		StartWallRun(RightHit, EWallSide::Right);
	}
}

bool UApexMovementComponent::TraceForWall(EWallSide Side, FHitResult& OutHit) const
{
	if (!CharacterOwner || !MovementSettings)
	{
		return false;
	}

	const FVector Start = CharacterOwner->GetActorLocation();
	const FVector Right = CharacterOwner->GetActorRightVector();
	const FVector TraceDir = (Side == EWallSide::Left) ? -Right : Right;
	const FVector End = Start + TraceDir * MovementSettings->WallRunCheckDistance;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(CharacterOwner);

	if (GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params))
	{
		return IsValidWallRunSurface(OutHit);
	}

	return false;
}

bool UApexMovementComponent::IsValidWallRunSurface(const FHitResult& Hit) const
{
	if (!Hit.bBlockingHit)
	{
		return false;
	}

	const float WallAngle = FMath::Abs(Hit.Normal.Z);
	if (WallAngle > 0.3f)
	{
		return false;
	}

	if (Hit.GetActor() == LastWallRunActor.Get() && WallRunSameWallCooldown > 0.0f)
	{
		return false;
	}

	return true;
}

bool UApexMovementComponent::IsAboveGround() const
{
	if (!CharacterOwner || !MovementSettings)
	{
		return false;
	}

	const FVector Start = CharacterOwner->GetActorLocation();
	const FVector End = Start - FVector(0, 0, MovementSettings->WallRunMinHeight);

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(CharacterOwner);

	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
}

float UApexMovementComponent::CalculateWallRunBoost(float ParallelSpeed) const
{
	if (!MovementSettings)
	{
		return 0.0f;
	}

	const float MinBoost = MovementSettings->WallRunMinBoost;
	const float MaxBoost = MovementSettings->WallRunMaxBoost;
	const float MinSpeed = MovementSettings->WallRunMinSpeed;
	const float BoostCap = MovementSettings->WallRunBoostCap;

	// If speed is below minimum, no wallrun happens (handled elsewhere)
	if (ParallelSpeed < MinSpeed)
	{
		return 0.0f;
	}

	// If speed is above BoostCap, give MaxBoost
	if (ParallelSpeed > BoostCap)
	{
		return MaxBoost;
	}

	// Calculate ratio for interpolation
	// ratio = (BoostCap - speed) / (BoostCap - MinSpeed)
	// At speed = BoostCap: ratio = 0 -> MinBoost (via Lerp)
	// At speed = MinSpeed: ratio = 1 -> MaxBoost (via Lerp)
	const float Denominator = BoostCap - MinSpeed;
	if (Denominator <= 0.0f)
	{
		// Invalid configuration, return MinBoost
		return MinBoost;
	}

	const float Ratio = (BoostCap - ParallelSpeed) / Denominator;

	// Only interpolate if ratio < 1.0
	// Note: We do NOT clamp ratio to allow MinBoost > MaxBoost if desired
	if (Ratio < 1.0f)
	{
		return FMath::Lerp(MinBoost, MaxBoost, Ratio);
	}

	// ratio >= 1.0 means speed is at or below MinSpeed
	return MaxBoost;
}

void UApexMovementComponent::StartWallRun(const FHitResult& WallHit, EWallSide Side)
{
	if (!MovementSettings)
	{
		return;
	}

	// Calculate direction along wall
	FVector AlongWall = FVector::CrossProduct(WallHit.Normal, FVector::UpVector);

	if (FVector::DotProduct(Velocity, AlongWall) < 0)
	{
		AlongWall = -AlongWall;
	}
	FVector WallDirection = AlongWall.GetSafeNormal();

	// Forward direction check
	if (CharacterOwner)
	{
		FVector PlayerForward = CharacterOwner->GetActorForwardVector();
		PlayerForward.Z = 0.0f;
		PlayerForward.Normalize();

		float ForwardDot = FVector::DotProduct(PlayerForward, WallDirection);

		if (ForwardDot < 0.3f)
		{
			return;
		}
	}

	// Calculate parallel speed for boost calculation
	const float ParallelSpeed = FMath::Abs(FVector::DotProduct(Velocity, WallDirection));

	// Check if speed is too low for wallrun
	if (ParallelSpeed < MovementSettings->WallRunMinSpeed)
	{
		return;
	}

	bIsWallRunning = true;
	WallRunSide = Side;
	WallRunNormal = WallHit.Normal;
	WallRunTimeRemaining = MovementSettings->WallRunMaxDuration;
	WallRunDirection = WallDirection;

	// Store entry velocity for momentum preservation
	WallRunEntryVelocity = Velocity;

	// Reset jump count and wallrun end reason
	CurrentJumpCount = 0;
	LastWallRunEndReason = EWallRunEndReason::None;
	LastWallRunActor = WallHit.GetActor();

	// Calculate and apply speed boost
	const float BoostAmount = CalculateWallRunBoost(ParallelSpeed);
	if (BoostAmount > 0.0f)
	{
		// Apply boost in wall direction
		Velocity += WallDirection * BoostAmount;
		UE_LOG(LogWallRun, Log, TEXT("WallRun Boost: +%.1f (parallel speed was %.1f)"), BoostAmount, ParallelSpeed);
	}

	// Apply smaller capsule (Titanfall 2 style - NO TILT)
	ApplyWallRunCapsule();

	UE_LOG(LogWallRun, Warning, TEXT("=== WALLRUN STARTED === Speed=%.1f (after boost), Side=%s"),
		Velocity.Size2D(), Side == EWallSide::Left ? TEXT("Left") : TEXT("Right"));

	OnWallRunChanged.Broadcast(true, Side);
	OnWallrunStarted.Broadcast(Side);
}

void UApexMovementComponent::EndWallRun(EWallRunEndReason Reason)
{
	if (!bIsWallRunning)
	{
		return;
	}

	UE_LOG(LogWallRun, Log, TEXT("WALLRUN ENDED: FinalSpeed=%.1f, Reason=%d"), Velocity.Size2D(), static_cast<int32>(Reason));

	bIsWallRunning = false;
	WallRunSide = EWallSide::None;
	LastWallRunEndReason = Reason;

	// Restore normal capsule
	RestoreWallRunCapsule();

	if (MovementSettings)
	{
		WallRunSameWallCooldown = MovementSettings->WallRunSameWallCooldown;
	}

	// Set jump availability based on end reason
	if (Reason == EWallRunEndReason::JumpedOff)
	{
		// Player jumped off - no more jumps allowed
		CurrentJumpCount = MovementSettings ? MovementSettings->MaxJumpCount : 2;
	}
	else if (Reason == EWallRunEndReason::TimeExpired)
	{
		// Time expired - allow one more jump (will be handled in DoJump)
		// CurrentJumpCount stays at 0, but LastWallRunEndReason tracks this
		CurrentJumpCount = 0;
	}
	// For LostWall, keep current jump count

	OnWallRunChanged.Broadcast(false, EWallSide::None);
	OnWallrunEnded.Broadcast();
}

void UApexMovementComponent::UpdateWallRun(float DeltaTime)
{
	if (!MovementSettings)
	{
		EndWallRun(EWallRunEndReason::LostWall);
		return;
	}

	// Time limit (gravity stays disabled for this duration)
	WallRunTimeRemaining -= DeltaTime;
	if (WallRunTimeRemaining <= 0.0f)
	{
		EndWallRun(EWallRunEndReason::TimeExpired);
		return;
	}

	// Verify wall is still there
	FHitResult WallHit;
	if (!TraceForWall(WallRunSide, WallHit))
	{
		EndWallRun(EWallRunEndReason::LostWall);
		return;
	}

	// Update wall normal
	WallRunNormal = WallHit.Normal;

	// Recalculate direction along wall
	FVector AlongWall = FVector::CrossProduct(WallRunNormal, FVector::UpVector);
	if (FVector::DotProduct(WallRunDirection, AlongWall) < 0)
	{
		AlongWall = -AlongWall;
	}
	WallRunDirection = AlongWall.GetSafeNormal();

	// ===== MOMENTUM-BASED MOVEMENT (like floor slide) =====
	float CurrentSpeed = FVector::DotProduct(Velocity, WallRunDirection);

	// Apply deceleration
	const float WallDecel = MovementSettings->WallRunDeceleration;
	CurrentSpeed = FMath::Max(CurrentSpeed - WallDecel * DeltaTime, 0.0f);

	// End if too slow
	if (CurrentSpeed < MovementSettings->WallRunEndSpeed)
	{
		UE_LOG(LogWallRun, Warning, TEXT("Wallrun ended: speed %.1f < min %.1f"),
			CurrentSpeed, MovementSettings->WallRunEndSpeed);
		EndWallRun(EWallRunEndReason::LostWall);
		return;
	}

	// Apply velocity along wall direction
	Velocity = WallRunDirection * CurrentSpeed;

	// NO GRAVITY during wallrun
	Velocity.Z = 0.0f;

	// Stick to wall
	FVector ToWall = -WallRunNormal * 50.0f;
	Velocity += ToWall * DeltaTime;

	UE_LOG(LogWallRun, Log, TEXT("WALLRUN: Speed=%.1f, TimeLeft=%.2f"), CurrentSpeed, WallRunTimeRemaining);
}

void UApexMovementComponent::UpdateWallRunCameraTilt(float DeltaTime)
{
	if (!MovementSettings)
	{
		CurrentWallRunCameraRoll = 0.0f;
		CurrentWallRunCameraOffset = FVector::ZeroVector;
		CurrentWallRunMeshRoll = 0.0f;
		CurrentWallRunMeshPitch = 0.0f;
		CurrentWallRunCameraTilt = FRotator::ZeroRotator;
		CurrentCameraTilt = FRotator::ZeroRotator;
		CurrentCameraOffset = FVector::ZeroVector;
		return;
	}

	// Target values
	float TargetCameraRoll = 0.0f;
	FVector TargetCameraOffset = FVector::ZeroVector;
	float TargetMeshRoll = 0.0f;
	float TargetMeshPitch = 0.0f;

	if (bIsWallRunning)
	{
		// Direction multiplier - Left wall = +, Right wall = -
		const float DirectionMult = (WallRunSide == EWallSide::Left) ? 1.0f : -1.0f;

		// Camera roll
		TargetCameraRoll = MovementSettings->WallRunCameraRoll * DirectionMult;

		// Mesh tilt - Pitch controls side tilt due to mesh orientation in Blueprint
		TargetMeshRoll = MovementSettings->WallRunMeshTiltRoll;  // No direction mult
		TargetMeshPitch = MovementSettings->WallRunMeshTiltPitch * DirectionMult;  // Direction mult here

		UE_LOG(LogWallRun, Warning, TEXT("WallRun Tilt: Side=%s, DirMult=%.1f, CamRoll=%.2f, MeshPitch=%.2f"),
			WallRunSide == EWallSide::Left ? TEXT("LEFT") : TEXT("RIGHT"),
			DirectionMult, TargetCameraRoll, TargetMeshPitch);

		// Camera offset - use side-specific offsets
		if (WallRunSide == EWallSide::Left)
		{
			TargetCameraOffset = MovementSettings->WallRunCameraOffsetLeft;
		}
		else
		{
			TargetCameraOffset = MovementSettings->WallRunCameraOffsetRight;
		}
	}

	// Interpolate camera roll
	CurrentWallRunCameraRoll = FMath::FInterpTo(
		CurrentWallRunCameraRoll,
		TargetCameraRoll,
		DeltaTime,
		MovementSettings->WallRunCameraTiltSpeed
	);

	// Interpolate mesh roll - EXACT same as camera
	CurrentWallRunMeshRoll = FMath::FInterpTo(
		CurrentWallRunMeshRoll,
		TargetMeshRoll,
		DeltaTime,
		MovementSettings->WallRunCameraTiltSpeed
	);

	// Interpolate mesh pitch
	CurrentWallRunMeshPitch = FMath::FInterpTo(
		CurrentWallRunMeshPitch,
		TargetMeshPitch,
		DeltaTime,
		MovementSettings->WallRunCameraTiltSpeed
	);

	// Interpolate camera offset
	CurrentWallRunCameraOffset = FMath::VInterpTo(
		CurrentWallRunCameraOffset,
		TargetCameraOffset,
		DeltaTime,
		MovementSettings->WallRunCameraTiltSpeed
	);

	// Update internal camera tilt (used by GetWallRunCameraTilt)
	CurrentWallRunCameraTilt.Roll = CurrentWallRunCameraRoll;

	// Update deprecated vars for backwards compatibility
	CurrentCameraTilt = CurrentWallRunCameraTilt;
	CurrentCameraOffset = CurrentWallRunCameraOffset;
}

// ==================== WallRun Capsule (Titanfall 2 style - size only, no tilt) ====================

void UApexMovementComponent::ApplyWallRunCapsule()
{
	if (!CharacterOwner || !MovementSettings || bWallRunCapsuleModified)
	{
		return;
	}

	// Check if feature is enabled
	if (!MovementSettings->bEnableWallRunCapsuleTilt)
	{
		return;
	}

	UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	if (!Capsule)
	{
		return;
	}

	// Store original values
	WallRunOriginalCapsuleHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	WallRunOriginalCapsuleRadius = Capsule->GetUnscaledCapsuleRadius();

	// Shrink capsule to wallrun height (NO TILT to avoid rotating FirstPersonMesh)
	float WallRunHalfHeight = MovementSettings->WallRunCapsuleHalfHeight;
	Capsule->SetCapsuleHalfHeight(WallRunHalfHeight);

	bWallRunCapsuleModified = true;

	UE_LOG(LogWallRun, Log, TEXT("WallRun Capsule Applied: Height=%.1f"), WallRunHalfHeight);
}

void UApexMovementComponent::RestoreWallRunCapsule()
{
	if (!CharacterOwner || !bWallRunCapsuleModified)
	{
		return;
	}

	UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	if (!Capsule)
	{
		return;
	}

	// Restore original height immediately
	Capsule->SetCapsuleHalfHeight(WallRunOriginalCapsuleHalfHeight);

	bWallRunCapsuleModified = false;

	UE_LOG(LogWallRun, Log, TEXT("WallRun Capsule Restored: Height -> %.1f"), WallRunOriginalCapsuleHalfHeight);
}

// ==================== Wall Bounce ====================

bool UApexMovementComponent::CanWallBounce() const
{
	if (!MovementSettings || !MovementSettings->bEnableWallBounce)
	{
		return false;
	}

	if (bIsSliding || bIsMantling || bIsWallRunning || !IsFalling())
	{
		return false;
	}

	if (WallBounceCooldownRemaining > 0.0f)
	{
		return false;
	}

	return true;
}

void UApexMovementComponent::CheckForWallBounce()
{
	if (!CanWallBounce() || !CharacterOwner || !MovementSettings)
	{
		return;
	}

	// Trace forward in velocity direction
	const FVector VelDir = Velocity.GetSafeNormal();
	if (VelDir.IsNearlyZero())
	{
		return;
	}

	const float TraceDistance = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius() + 30.0f;
	const FVector Start = CharacterOwner->GetActorLocation();
	const FVector End = Start + VelDir * TraceDistance;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(CharacterOwner);

	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return;
	}

	// Check if wall is vertical enough
	if (FMath::Abs(Hit.Normal.Z) > 0.3f)
	{
		return;
	}

	// Calculate perpendicular velocity component
	const float PerpendicularSpeed = FMath::Abs(FVector::DotProduct(Velocity, -Hit.Normal));

	if (PerpendicularSpeed < MovementSettings->WallBounceMinSpeed)
	{
		return;
	}

	// Check angle
	const float HitAngle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(VelDir, -Hit.Normal)));
	if (HitAngle > MovementSettings->WallBounceMinAngle)
	{
		return;
	}

	PerformWallBounce(Hit);
}

void UApexMovementComponent::PerformWallBounce(const FHitResult& WallHit)
{
	if (!MovementSettings)
	{
		return;
	}

	const FVector Normal = WallHit.Normal;
	const float DotProduct = FVector::DotProduct(Velocity, Normal);

	// Reflect with elasticity
	FVector ReflectedVelocity = Velocity - (1.0f + MovementSettings->WallBounceElasticity) * DotProduct * Normal;

	UE_LOG(LogWallRun, Warning, TEXT("=== WALL BOUNCE === InSpeed=%.1f, OutSpeed=%.1f, Elasticity=%.2f"),
		Velocity.Size(), ReflectedVelocity.Size(), MovementSettings->WallBounceElasticity);

	Velocity = ReflectedVelocity;
	WallBounceCooldownRemaining = MovementSettings->WallBounceCooldown;

	OnWallBounce.Broadcast(ReflectedVelocity.GetSafeNormal());
}

// ==================== Mantle ====================

bool UApexMovementComponent::CanMantle() const
{
	if (!MovementSettings || bIsMantling || bIsSliding || bIsWallRunning || !IsFalling())
	{
		return false;
	}

	FHitResult Hit;
	return TraceMantleSurface(Hit);
}

void UApexMovementComponent::TryMantle()
{
	FHitResult Hit;
	if (!TraceMantleSurface(Hit))
	{
		return;
	}

	bIsMantling = true;
	MantleStartLocation = CharacterOwner->GetActorLocation();
	MantleTargetLocation = Hit.Location + FVector(0, 0, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	MantleAlpha = 0.0f;
	Velocity = FVector::ZeroVector;
	SetMovementMode(MOVE_Flying);
}

void UApexMovementComponent::UpdateMantle(float DeltaTime)
{
	if (!MovementSettings)
	{
		bIsMantling = false;
		SetMovementMode(MOVE_Falling);
		return;
	}

	MantleAlpha += DeltaTime / MovementSettings->MantleDuration;

	if (MantleAlpha >= 1.0f)
	{
		CharacterOwner->SetActorLocation(MantleTargetLocation);
		bIsMantling = false;
		SetMovementMode(MOVE_Walking);
		return;
	}

	const float SmoothAlpha = FMath::InterpEaseOut(0.0f, 1.0f, MantleAlpha, 2.0f);
	CharacterOwner->SetActorLocation(FMath::Lerp(MantleStartLocation, MantleTargetLocation, SmoothAlpha));
}

bool UApexMovementComponent::TraceMantleSurface(FHitResult& OutHit) const
{
	if (!CharacterOwner || !MovementSettings)
	{
		return false;
	}

	const FVector Start = CharacterOwner->GetActorLocation() + FVector(0, 0, 50.0f);
	const FVector Forward = CharacterOwner->GetActorForwardVector();
	const FVector End = Start + Forward * MovementSettings->MantleTraceDistance;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(CharacterOwner);

	FHitResult WallHit;
	if (!GetWorld()->LineTraceSingleByChannel(WallHit, Start, End, ECC_Visibility, Params))
	{
		return false;
	}

	const FVector LedgeTraceStart = WallHit.Location + Forward * 10.0f + FVector(0, 0, MovementSettings->MantleReachHeight);
	const FVector LedgeTraceEnd = WallHit.Location + Forward * 10.0f;

	if (!GetWorld()->LineTraceSingleByChannel(OutHit, LedgeTraceStart, LedgeTraceEnd, ECC_Visibility, Params))
	{
		return false;
	}

	return OutHit.Normal.Z > 0.7f;
}

// ==================== Air Movement ====================

void UApexMovementComponent::ApplyAirStrafe(float DeltaTime)
{
	if (!MovementSettings || MovementSettings->AirStrafeMultiplier <= 0.0f)
	{
		return;
	}

	const FVector InputVector = GetLastInputVector();
	if (InputVector.IsNearlyZero())
	{
		return;
	}

	FVector VelNorm = Velocity.GetSafeNormal2D();
	FVector WishDir = InputVector.GetSafeNormal2D();
	float VelDotWish = FVector::DotProduct(VelNorm, WishDir);

	if (FMath::Abs(VelDotWish) > 0.9f)
	{
		return;
	}

	const float CurrentSpeed = Velocity.Size2D();
	const float AddSpeed = FMath::Clamp(
		MovementSettings->MaxAirStrafeSpeed - CurrentSpeed * VelDotWish,
		0.0f,
		MovementSettings->AirAcceleration * DeltaTime * MovementSettings->AirStrafeMultiplier
	);

	if (AddSpeed > 0.0f)
	{
		Velocity += WishDir * AddSpeed;
	}
}

void UApexMovementComponent::UpdateJumpHold(float DeltaTime)
{
	if (!MovementSettings || !bJumpHeld || JumpHoldTimeRemaining <= 0.0f)
	{
		return;
	}

	JumpHoldTimeRemaining -= DeltaTime;

	if (Velocity.Z > 0.0f)
	{
		Velocity.Z += MovementSettings->JumpHoldForce * DeltaTime;
	}
}

// ==================== Air Dash ====================

bool UApexMovementComponent::CanAirDash() const
{
	if (!MovementSettings || !IsFalling() || bIsAirDashing || bIsMantling || bIsWallRunning)
	{
		return false;
	}

	if (AirDashCooldownRemaining > 0.0f)
	{
		return false;
	}

	return RemainingAirDashCount > 0;
}

void UApexMovementComponent::TryAirDash()
{
	if (!CanAirDash() || !CharacterOwner)
	{
		return;
	}

	bIsAirDashing = true;
	RemainingAirDashCount--;

	FVector DashDirection;

	const FVector InputDir = GetLastInputVector();
	if (!InputDir.IsNearlyZero())
	{
		DashDirection = InputDir.GetSafeNormal();
	}
	else
	{
		DashDirection = CharacterOwner->GetActorForwardVector();
	}

	DashDirection.Z = 0.0f;
	DashDirection.Normalize();

	Velocity = DashDirection * MovementSettings->AirDashSpeed;
	Velocity.Z = 0.0f;

	// Start decay timer
	AirDashDecayTimeRemaining = MovementSettings->AirDashDecayDuration;
}

void UApexMovementComponent::UpdateAirDash(float DeltaTime)
{
	bIsAirDashing = false;

	if (MovementSettings)
	{
		AirDashCooldownRemaining = MovementSettings->AirDashCooldown;
	}
}

void UApexMovementComponent::UpdateAirDashDecay(float DeltaTime)
{
	if (!MovementSettings || !CharacterOwner)
	{
		AirDashDecayTimeRemaining = 0.0f;
		return;
	}

	AirDashDecayTimeRemaining -= DeltaTime;

	// Trace down to find ground height
	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(CharacterOwner);

	const FVector Start = CharacterOwner->GetActorLocation();
	const FVector End = Start - FVector(0.0f, 0.0f, MovementSettings->AirDashDecayMaxHeight + 100.0f);

	float HeightAboveGround = MovementSettings->AirDashDecayMaxHeight;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		HeightAboveGround = Hit.Distance;
	}

	// Calculate decay strength based on height
	const float MinHeight = MovementSettings->AirDashDecayMinHeight;
	const float MaxHeight = MovementSettings->AirDashDecayMaxHeight;
	const float HeightAlpha = 1.0f - FMath::Clamp((HeightAboveGround - MinHeight) / (MaxHeight - MinHeight), 0.0f, 1.0f);

	if (HeightAlpha <= 0.0f)
	{
		return; // Above max height, no decay
	}

	// Apply decay to horizontal speed
	FVector HorizontalVel = FVector(Velocity.X, Velocity.Y, 0.0f);
	float HorizontalSpeed = HorizontalVel.Size();

	if (HorizontalSpeed <= MovementSettings->AirDashMinSpeed)
	{
		return; // Already at or below minimum
	}

	const float DecayAmount = MovementSettings->AirDashDecayRate * HeightAlpha * DeltaTime;
	const float NewSpeed = FMath::Max(HorizontalSpeed - DecayAmount, MovementSettings->AirDashMinSpeed);

	if (HorizontalSpeed > 0.0f)
	{
		const float SpeedRatio = NewSpeed / HorizontalSpeed;
		Velocity.X *= SpeedRatio;
		Velocity.Y *= SpeedRatio;
	}
}

void UApexMovementComponent::ResetAirAbilities()
{
	RemainingAirDashCount = MovementSettings ? MovementSettings->MaxAirDashCount : 1;
	bIsAirDashing = false;
}

// ==================== EMF ====================

void UApexMovementComponent::SetEMFForce(const FVector& Force)
{
	CurrentEMFForce = Force;
}

void UApexMovementComponent::ApplyEMFForces(float DeltaTime)
{
	if (!MovementSettings || CurrentEMFForce.IsNearlyZero())
	{
		return;
	}

	Velocity += CurrentEMFForce * DeltaTime * MovementSettings->EMFForceMultiplier;

	if (Velocity.Size() > MovementSettings->MaxEMFVelocity)
	{
		Velocity = Velocity.GetSafeNormal() * MovementSettings->MaxEMFVelocity;
	}
}

// ==================== Utility ====================

void UApexMovementComponent::UpdateMovementState()
{
	EPolarityMovementState NewState = EPolarityMovementState::None;

	if (bIsMantling)
		NewState = EPolarityMovementState::Mantling;
	else if (bIsWallRunning)
		NewState = EPolarityMovementState::WallRunning;
	else if (bIsSliding)
		NewState = EPolarityMovementState::Sliding;
	else if (IsFalling())
		NewState = EPolarityMovementState::Falling;
	else if (IsCrouching())
		NewState = EPolarityMovementState::Crouching;
	else if (IsSprinting())
		NewState = EPolarityMovementState::Sprinting;
	else if (IsMovingOnGround())
		NewState = EPolarityMovementState::Walking;

	SetMovementState(NewState);
}

void UApexMovementComponent::SetMovementState(EPolarityMovementState NewState)
{
	if (CurrentMovementState != NewState)
	{
		EPolarityMovementState OldState = CurrentMovementState;
		CurrentMovementState = NewState;
		OnMovementStateChanged.Broadcast(OldState, NewState);
	}
}

float UApexMovementComponent::GetSpeedRatio() const
{
	if (!MovementSettings || MovementSettings->SprintSpeed <= 0.0f)
	{
		return 0.0f;
	}
	return Velocity.Size2D() / MovementSettings->SprintSpeed;
}

// ==================== Velocity Modifiers ====================

void UApexMovementComponent::RegisterVelocityModifier(TScriptInterface<IVelocityModifier> Modifier)
{
	if (Modifier && !VelocityModifiers.Contains(Modifier))
	{
		VelocityModifiers.Add(Modifier);
	}
}

void UApexMovementComponent::UnregisterVelocityModifier(TScriptInterface<IVelocityModifier> Modifier)
{
	if (Modifier)
	{
		VelocityModifiers.Remove(Modifier);
	}
}

void UApexMovementComponent::ApplyVelocityModifiers(float DeltaTime)
{
	for (const TScriptInterface<IVelocityModifier>& Modifier : VelocityModifiers)
	{
		if (Modifier)
		{
			FVector VelocityDelta = FVector::ZeroVector;

			if (IVelocityModifier::Execute_ModifyVelocity(Modifier.GetObject(), DeltaTime, Velocity, VelocityDelta))
			{
				Velocity += VelocityDelta;
			}
		}
	}
}

void UApexMovementComponent::PlayCameraShake(TSubclassOf<UCameraShakeBase> CameraShake)
{
	if (!CameraShake || !OwnerController)
	{
		return;
	}

	OwnerController->ClientStartCameraShake(CameraShake);
}