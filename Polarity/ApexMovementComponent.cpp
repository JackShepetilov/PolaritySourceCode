// ApexMovementComponent.cpp
// Titanfall 2 / Apex Legends style movement implementation

#include "ApexMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "MovementSettings.h"
#include "PolarityCharacter.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "VelocityModifier.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSlide, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogWallRun, Log, All);

UApexMovementComponent::UApexMovementComponent()
{
	// Needed for the two state bools below: without it the component's own properties never leave
	// the server and observers cannot tell a sliding character from a standing one.
	SetIsReplicatedByDefault(true);

	// Send our own move format instead of the stock one. This is what carries EPolarityMoveFlag; the
	// container must live as long as the component, hence the member.
	SetNetworkMoveDataContainer(PolarityMoveDataContainer);

	NavAgentProps.bCanCrouch = true;
	bCanWalkOffLedgesWhenCrouching = true;
	SetCrouchedHalfHeight(50.0f);

	// Keep the feet planted when the capsule shrinks. A capsule resizes around its centre, so
	// without this the character would rise by the height difference; the engine drops the capsule
	// by that amount instead, once per transition and with the encroachment check that Crouch()
	// already does. This replaces the hand-rolled per-frame SetActorLocation that used to live in
	// UpdateCapsuleHeight and had no collision check at all.
	bCrouchMaintainsBaseLocation = true;

	AirControl = 0.0f; // Disabled - using custom ApplyAirStrafe() instead
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

		GravityScale = MovementSettings->DefaultGravityScale;
		JumpZVelocity = MovementSettings->JumpZVelocity;
		MaxWalkSpeed = MovementSettings->WalkSpeed;
		MaxWalkSpeedCrouched = MovementSettings->CrouchSpeed;
		GroundFriction = MovementSettings->GroundFriction;
		BrakingDecelerationWalking = MovementSettings->BrakingDeceleration;
		// Native AirControl disabled - all air movement handled by ApplyAirStrafe()
		AirControl = 0.0f;

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
	// Fold the sprint key and any veto into the one value that gets simulated and sent. Done here,
	// before the move for this frame is built, so the flag the server receives is the flag the
	// character actually moved with.
	//
	// Locally controlled only. On a machine that merely simulates this character (the server for a
	// remote player, or another client) there is no key and no veto, and bWantsToSprint arrives
	// through UpdateFromCompressedFlags or replicated movement; recomputing it here would
	// overwrite the truth with a local guess of "not sprinting".
	if (PawnOwner && PawnOwner->IsLocallyControlled())
	{
		bWantsToSprint = bSprintKeyHeld && !IsSprintSuppressed();
	}

	// Slide cooldowns and slide fatigue used to be counted down here. They moved into
	// OnMovementUpdated: they gate slide entry and scale the slide jump, so they have to advance
	// with the simulated move and rewind with it, or the client replays a jump with numbers the
	// original simulation never had and the server hands back a different velocity.
	// Every cooldown that gates a movement decision moved into OnMovementUpdated with the slide ones.
	// A tick-driven timer counts at the local frame rate and does not rewind during a replay, so the
	// two ends end up gating different frames and the dash a client saw never happens on the server.

	// The capsule is no longer resized here. Crouching goes through bWantsToCrouch, and the engine
	// swaps the capsule in one step with a fit check and a matching mesh offset. Resizing it every
	// frame is what pushed the feet through the floor, left the character hanging above it, and
	// made observed characters bob: the per-frame teleport had no collision check, and on watching
	// machines it fought network smoothing, which already offsets the mesh.
	//
	// The *look* of a smooth crouch is a camera and mesh matter, and it already lives in
	// APolarityCharacter::AccumulateFirstPersonPose (CrouchSlideProgress, CrouchCameraOffset).

	// The pre-physics mechanics block moved into UpdateCharacterStateBeforeMovement, which the engine
	// calls from inside PerformMovement. Everything in it writes Velocity, and from the tick that
	// happened on one machine only: the server replayed the client's move without it and corrected
	// the client back every frame, which is why these read as "works on the host, broken elsewhere".

	// Jump hold moved into PhysFalling with the air strafe: it writes Velocity.Z, so from the tick
	// it produced a taller jump on the machine that pressed the key than on the one that replayed
	// the move.

#if ENABLE_DRAW_DEBUG
	// Track max jump height while in air
	if (bTrackingJump && IsFalling() && CharacterOwner)
	{
		const float CurrentZ = CharacterOwner->GetActorLocation().Z;
		if (CurrentZ > JumpMaxZ)
		{
			JumpMaxZ = CurrentZ;
		}

		const float CurrentHeight = CurrentZ - JumpStartZ;
		const float MaxHeight = JumpMaxZ - JumpStartZ;
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(8810, 0.0f, FColor::White,
				FString::Printf(TEXT("JUMP [%s]: Current=%.0f UU (%.2fm) | Peak=%.0f UU (%.2fm) | Vel.Z=%.0f"),
					*LastJumpType, CurrentHeight, CurrentHeight / 100.0f, MaxHeight, MaxHeight / 100.0f, Velocity.Z));
		}
	}
#endif

	// Air crouch hold detection
	if (bWantsSlideOnLand && IsFalling() && !bIsWallRunning && !bIsAirDashing && MovementSettings)
	{
		AirCrouchHoldTime += DeltaTime;

		// If held longer than threshold and not yet crouched, enable air crouch
		if (AirCrouchHoldTime >= MovementSettings->AirCrouchHoldThreshold && !bIsCrouchedInAir)
		{
			bIsCrouchedInAir = true;
			StartCrouching();
		}
	}
	else if (!IsFalling() || !bWantsSlideOnLand)
	{
		// Reset air crouch when landing or button released
		if (bIsCrouchedInAir && !bIsSliding)
		{
			bIsCrouchedInAir = false;
			// Don't call StopCrouching here - ProcessLanded will handle transition to slide
		}
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

	// A blocking hit can zero the velocity during the parent movement tick. End the dash instead
	// of restoring its full speed into the obstacle on the next frame.
	if (bIsGroundDashing && (!IsMovingOnGround() || Velocity.Size2D() < GroundDashSpeed * 0.35f))
	{
		EndGroundDash();
	}

	// Slide deceleration moved into OnMovementUpdated so that it runs inside the simulated move.

	// POST-TICK: Also check wall bounce for air crouch after physics
	if (bIsCrouchedInAir && IsFalling())
	{
		CheckForWallBounce();
	}

	// POST-TICK: Containment fields are elastic for the player on ANY contact
	// (not just slide/air-crouch) — reuse the wall-bounce reflection directly.
	if (MovementSettings && WallBounceCooldownRemaining <= 0.0f && CharacterOwner
		&& !bIsMantling && !bIsWallRunning)
	{
		const float Speed = Velocity.Size();
		if (Speed > 350.0f)
		{
			const FVector VelDir = Velocity.GetSafeNormal();
			UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
			const float CapsuleRadius = Capsule ? Capsule->GetScaledCapsuleRadius() : 35.0f;
			FCollisionQueryParams FieldParams;
			FieldParams.AddIgnoredActor(CharacterOwner);
			FHitResult FieldHit;
			const FVector Start = CharacterOwner->GetActorLocation();
			const FVector End = Start + VelDir * (CapsuleRadius + 40.0f);
			if (GetWorld()->SweepSingleByChannel(FieldHit, Start, End, FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeSphere(CapsuleRadius * 0.8f), FieldParams)
				&& FieldHit.GetActor() && FieldHit.GetActor()->ActorHasTag(FName("ContainmentField")))
			{
				PerformWallBounce(FieldHit);
				// The sweep bounces the player BEFORE a real blocking hit lands, so the
				// field's Event Hit (hit-reveal shader) never fires - notify it manually.
				FieldHit.GetActor()->ReceiveHit(FieldHit.GetComponent(), CharacterOwner,
					Capsule, false, FieldHit.ImpactPoint, FieldHit.ImpactNormal,
					Velocity, FieldHit);
			}
		}
	}

	UpdateMovementState();
}

float UApexMovementComponent::GetMaxSpeed() const
{
	if (!MovementSettings)
	{
		const float BaseSpeed = Super::GetMaxSpeed() * DamageSpeedMultiplier * ExternalSpeedMultiplier;
		return bExternalMaxSpeedOverride ? FMath::Max(BaseSpeed, ExternalMaxSpeedOverride) : BaseSpeed;
	}

	float BaseSpeed;

	if (bIsGroundDashing)
	{
		BaseSpeed = GroundDashSpeed;
	}
	else if (bIsSliding || bIsWallRunning)
	{
		BaseSpeed = MovementSettings->SpeedCap;
	}
	else if (IsCrouching())
	{
		BaseSpeed = MovementSettings->CrouchSpeed;
	}
	else if (IsSprinting())
	{
		BaseSpeed = MovementSettings->SprintSpeed;
	}
	else
	{
		switch (MovementMode)
		{
		case MOVE_Walking:
		case MOVE_NavWalking:
			BaseSpeed = MovementSettings->WalkSpeed;
			break;
		case MOVE_Falling:
			BaseSpeed = MovementSettings->SprintSpeed;
			break;
		default:
			BaseSpeed = Super::GetMaxSpeed();
			break;
		}
	}

	const float ScaledBaseSpeed = BaseSpeed * DamageSpeedMultiplier * ExternalSpeedMultiplier;
	return bExternalMaxSpeedOverride ? FMath::Max(ScaledBaseSpeed, ExternalMaxSpeedOverride) : ScaledBaseSpeed;
}

void UApexMovementComponent::SetExternalMaxSpeedOverride(float MaxSpeed)
{
	bExternalMaxSpeedOverride = MaxSpeed > 0.0f;
	ExternalMaxSpeedOverride = bExternalMaxSpeedOverride ? MaxSpeed : 0.0f;
}

void UApexMovementComponent::ClearExternalMaxSpeedOverride()
{
	bExternalMaxSpeedOverride = false;
	ExternalMaxSpeedOverride = 0.0f;
}

void UApexMovementComponent::SetExternalSlideSpeedBurstOverride(float MinBurst, float MaxBurst)
{
	bExternalSlideSpeedBurstOverride = true;
	ExternalSlideMinSpeedBurst = FMath::Max(0.0f, MinBurst);
	ExternalSlideMaxSpeedBurst = FMath::Max(0.0f, MaxBurst);
}

void UApexMovementComponent::ClearExternalSlideSpeedBurstOverride()
{
	bExternalSlideSpeedBurstOverride = false;
	ExternalSlideMinSpeedBurst = 0.0f;
	ExternalSlideMaxSpeedBurst = 0.0f;
}

float UApexMovementComponent::GetGroundDashCooldownDuration() const
{
	return MovementSettings ? MovementSettings->GroundDashCooldown : 0.0f;
}

float UApexMovementComponent::GetAirDashCooldownDuration() const
{
	return MovementSettings ? MovementSettings->AirDashCooldown : 0.0f;
}

int32 UApexMovementComponent::GetMaxAirDashCount() const
{
	return MovementSettings ? MovementSettings->MaxAirDashCount : 0;
}

float UApexMovementComponent::GetMaxAcceleration() const
{
	// Slide, wallrun and ground dash are momentum only, but that is enforced in CalcVelocity now,
	// not here. Returning 0 from this function does not just stop acceleration: ScaleInputAcceleration
	// multiplies the input by it, so the whole Acceleration vector becomes zero and the *direction*
	// the player is pushing is lost. That direction is the only piece of input the server receives,
	// and slide steering needs it, so it has to survive.
	if (!MovementSettings)
	{
		return Super::GetMaxAcceleration();
	}

	// In the air the engine's own lateral acceleration stays off — but through AirControl (0 in the
	// constructor), not through this. Returning 0 here would make ScaleInputAcceleration zero the
	// whole Acceleration vector, and Acceleration is how ApplyAirStrafe learns which way the player
	// is pushing on the server and during a replay. So keep the number honest and let AirControl do
	// the disabling: GetFallingLateralAcceleration multiplies by it and hands CalcVelocity a zero.
	return IsFalling() ? MovementSettings->AirAcceleration : MovementSettings->GroundAcceleration;
}

float UApexMovementComponent::GetMaxBrakingDeceleration() const
{
	// Keep the dash's authored speed for its short active window. Collision is still fully handled
	// by CharacterMovement; we only suppress friction/braking while the dash is active.
	if (bIsGroundDashing)
	{
		return 0.0f;
	}

	return Super::GetMaxBrakingDeceleration();
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

	UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s Landed: wantsSlideOnLand=%d preLandSpeed=%.0f fall=%.0f"),
		(GetOwnerRole() == ROLE_Authority ? TEXT("SERVER") : TEXT("CLIENT")), bWantsSlideOnLand ? 1 : 0, PreLandSpeed, LastFallVelocity);

	if (bWantsSlideOnLand && PreLandSpeed > 0.0f)
	{
		Velocity.X = PreLandHorizontalVelocity.X;
		Velocity.Y = PreLandHorizontalVelocity.Y;

		StartSlideFromAir(LastFallVelocity);
		//bWantsSlideOnLand = false;
	}

	// Clear air crouch state
	bIsCrouchedInAir = false;

	OnLanded_Movement.Broadcast(Hit);
}

bool UApexMovementComponent::TryJump()
{
	// Record the INTENT instead of jumping on the spot.
	//
	// ACharacter::Jump() only sets bPressedJump, and that flag is what rides the saved move to the
	// server (FSavedMove_Character carries it as FLAG_JumpPressed); the server unpacks it and calls
	// DoJump itself. Calling DoJump directly from input changed Velocity on this machine only. On a
	// listen server host that looks correct, because the host IS the authority, but on a client the
	// server never learns a jump happened and its position correction undoes it. That is exactly why
	// the host could jump in the first coop session and the client could not.
	//
	// All of the jump logic still lives in DoJump below, which the movement component calls on both
	// sides. The camera shake moved into NotifyJumpPerformed, so it fires from wherever the jump
	// actually resolves rather than from the input handler.
	if (ACharacter* OwningCharacter = Cast<ACharacter>(GetOwner()))
	{
		OwningCharacter->Jump();
		return true;
	}

	return false;
}

void UApexMovementComponent::NotifyJumpPerformed(bool bWasAirJump)
{
	// Only the person actually holding this character should feel it: on a listen server DoJump
	// also runs for every remote player's character.
	const ACharacter* OwningCharacter = Cast<ACharacter>(GetOwner());
	if (OwningCharacter && OwningCharacter->IsLocallyControlled())
	{
		PlayCameraShake(JumpCameraShake);
	}

	OnJumpPerformed.Broadcast(bWasAirJump);
}

bool UApexMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	if (!MovementSettings)
	{
		return Super::DoJump(bReplayingMoves, DeltaTime);
	}

	const APolarityCharacter* PolChar = Cast<APolarityCharacter>(GetOwner());
	const bool bAllowAirJump = !PolChar || PolChar->bCanDoubleJump;
	const int32 MaxJumps = bAllowAirJump ? MovementSettings->MaxJumpCount : 1;

	// Wall jump - player pushed off wall, NO double jump allowed after
	if (bIsWallRunning)
	{
		FVector JumpVelocity = WallRunNormal * MovementSettings->WallJumpSideForce;
		JumpVelocity.Z = MovementSettings->WallJumpUpForce;

		// Add forward momentum: current wallrun speed + exit boost
		const float ForwardSpeed = WallRunCurrentSpeed + MovementSettings->WallRunExitBoost;
		JumpVelocity += WallRunDirection * ForwardSpeed;

		UE_LOG(LogWallRun, Warning, TEXT("WALL JUMP: ExitSpeed=%.1f (Current=%.1f + Boost=%.1f)"),
			ForwardSpeed, WallRunCurrentSpeed, MovementSettings->WallRunExitBoost);

		EndWallRun(EWallRunEndReason::JumpedOff);
		Velocity = JumpVelocity;
		SetMovementMode(MOVE_Falling);

		// After wall jump, allow one air jump (double jump) — unless air jumps are disabled
		CurrentJumpCount = bAllowAirJump ? (MaxJumps - 1) : MaxJumps;

#if ENABLE_DRAW_DEBUG
		JumpStartZ = CharacterOwner->GetActorLocation().Z;
		JumpMaxZ = JumpStartZ;
		bTrackingJump = true;
		LastJumpType = TEXT("WallJump");
#endif

		// Trigger Blueprint event
		if (CharacterOwner)
		{
			CharacterOwner->OnJumped();
		}

		// Broadcast jump event (wall jump is not considered double jump)
		NotifyJumpPerformed(false);

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

		// Broadcast jump event (slide jump counts as first jump)
		NotifyJumpPerformed(CurrentJumpCount > 1);

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

		// Broadcast jump event with double jump flag
		NotifyJumpPerformed(CurrentJumpCount > 1);

		return true;
	}

	return false;
}

// ==================== Input ====================

void UApexMovementComponent::StartSprint()
{
	if (const APolarityCharacter* PolChar = Cast<APolarityCharacter>(GetOwner()))
	{
		if (!PolChar->bCanSprint)
		{
			return;
		}
	}
	// Only the key state. bWantsToSprint is derived from it once per frame, so that the value the
	// move is packed with is the same one the character actually moved at.
	bSprintKeyHeld = true;
}

bool UApexMovementComponent::IsSprintSuppressed() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() < SprintSuppressedUntil;
}

void UApexMovementComponent::SuppressSprint(float Duration)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Length = (Duration < 0.0f) ? SprintSuppressionTime : Duration;

	// Never shorten an existing veto: two systems suppressing at once should give the longer of
	// the two, not whichever one happened to run last this frame.
	SprintSuppressedUntil = FMath::Max(SprintSuppressedUntil, World->GetTimeSeconds() + Length);
}

void UApexMovementComponent::StopSprint()
{
	bSprintKeyHeld = false;
}

uint16 UApexMovementComponent::PackPolarityMoveFlags() const
{
	uint16 Flags = 0;
	auto Set = [&Flags](bool bCondition, EPolarityMoveFlag Flag)
	{
		if (bCondition)
		{
			Flags |= static_cast<uint16>(Flag);
		}
	};

	Set(bWantsToSprint,     EPolarityMoveFlag::WantsToSprint);
	Set(bIsSliding,         EPolarityMoveFlag::Sliding);
	Set(bIsWallRunning,     EPolarityMoveFlag::WallRunning);
	Set(bWantsSlideOnLand,  EPolarityMoveFlag::WantsSlideOnLand);
	Set(bIsGroundDashing,   EPolarityMoveFlag::GroundDashing);
	Set(bIsAirDashing,      EPolarityMoveFlag::AirDashing);
	Set(bIsRedirecting,     EPolarityMoveFlag::AirDashRedirect);
	Set(bIsMantling,        EPolarityMoveFlag::Mantling);

	Set(bMeleeLungeWanted,        EPolarityMoveFlag::MeleeLunging);
	Set(bMeleeLungeHasTarget,     EPolarityMoveFlag::MeleeLungeHasTarget);
	Set(bMeleeLungeHoming,        EPolarityMoveFlag::MeleeLungeHoming);
	Set(bMeleeLungeRestoreOnEnd,  EPolarityMoveFlag::MeleeLungeRestore);
	Set(bMeleeDropKick,           EPolarityMoveFlag::MeleeDropKick);
	Set(bMeleeDropKickForward,    EPolarityMoveFlag::MeleeDropKickForward);

	return Flags;
}

void UApexMovementComponent::ApplyPolarityMoveFlags(uint16 Flags)
{
	auto Has = [Flags](EPolarityMoveFlag Flag)
	{
		return (Flags & static_cast<uint16>(Flag)) != 0;
	};

	// Plain intents: assigned straight across with no second-guessing. The byte already carries the
	// client's final answer, veto included, and re-deriving it here from state the server does not
	// have is exactly how the two ends drift apart.
	bWantsToSprint    = Has(EPolarityMoveFlag::WantsToSprint);
	bWantsSlideOnLand = Has(EPolarityMoveFlag::WantsSlideOnLand);
	bIsRedirecting    = Has(EPolarityMoveFlag::AirDashRedirect);

	// States that need a real entry. Each Start* sets up friction, gravity, direction and speed;
	// a side that only flipped the bool kept simulating normally and finished the move somewhere
	// else, which is what "works for one player and stutters for the other" looks like from inside.
	const bool bWantsSlide = Has(EPolarityMoveFlag::Sliding);
	if (bWantsSlide != bIsSliding)
	{
		if (bWantsSlide) { StartSlide(); } else { EndSlide(); }
	}
	// The client already made this call with state this machine does not have (its own cooldowns,
	// its own ground check). If CanSlide() disagreed inside StartSlide, the client's answer still
	// wins, otherwise the two would simulate different moves.
	bIsSliding = bWantsSlide;

	// Wallrun is the one case where the client's answer does NOT simply win. Only the decision
	// travels; the wall itself is geometry, so this side runs its own trace and uses its own normal
	// and direction. If it finds no wall, no wallrun happens here and the client gets corrected off
	// it, which is the correct outcome: a client cannot invent a wall.
	const bool bWantsWallRun = Has(EPolarityMoveFlag::WallRunning);
	if (bWantsWallRun && !bIsWallRunning)
	{
		CheckForWallRun();
	}
	else if (!bWantsWallRun && bIsWallRunning)
	{
		EndWallRun(EWallRunEndReason::LostWall);
	}

	// Dash and mantle end themselves from inside UpdateGroundDash / UpdateAirDash / UpdateMantle,
	// which now run inside the simulation on every machine, so both ends stop at the same simulated
	// moment on their own. Only the start needs driving, and the clearing below is a safety net for
	// the case where one side's Try* refused (a cooldown that had not elapsed there, a ledge it could
	// not find) and the two would otherwise stay disagreeing.
	const bool bWantsGroundDash = Has(EPolarityMoveFlag::GroundDashing);
	if (bWantsGroundDash && !bIsGroundDashing)
	{
		TryGroundDash();
	}
	else if (!bWantsGroundDash && bIsGroundDashing)
	{
		EndGroundDash();
	}

	const bool bWantsAirDash = Has(EPolarityMoveFlag::AirDashing);
	if (bWantsAirDash && !bIsAirDashing)
	{
		TryAirDash();
	}
	else if (!bWantsAirDash && bIsAirDashing)
	{
		bIsAirDashing = false;
	}

	// Mantle moves the character along a path it computed from the ledge it found. Same rule as
	// wallrun: the decision travels, the ledge is re-found locally.
	const bool bWantsMantle = Has(EPolarityMoveFlag::Mantling);
	if (bWantsMantle && !bIsMantling)
	{
		TryMantle();
	}
	else if (!bWantsMantle && bIsMantling)
	{
		bIsMantling = false;
	}

	// The lunge is four plain decisions and a destination, all taken as given; the destination
	// arrived with the move and was written into MeleeLungeTarget just before this call. Nothing is
	// started or ended here on purpose — UpdateCharacterStateBeforeMovement resolves the edge inside
	// the move a moment later, so the momentum capture and the miss impulse land on the same
	// simulated frame here as they did on the client.
	bMeleeLungeWanted       = Has(EPolarityMoveFlag::MeleeLunging);
	bMeleeLungeHasTarget    = Has(EPolarityMoveFlag::MeleeLungeHasTarget);
	bMeleeLungeHoming       = Has(EPolarityMoveFlag::MeleeLungeHoming);
	bMeleeLungeRestoreOnEnd = Has(EPolarityMoveFlag::MeleeLungeRestore);
	bMeleeDropKick          = Has(EPolarityMoveFlag::MeleeDropKick);
	bMeleeDropKickForward   = Has(EPolarityMoveFlag::MeleeDropKickForward);
}

void UApexMovementComponent::ApplyPolarityMoveFlagsForReplay(uint16 Flags)
{
	auto Has = [Flags](EPolarityMoveFlag Flag)
	{
		return (Flags & static_cast<uint16>(Flag)) != 0;
	};

	bWantsToSprint    = Has(EPolarityMoveFlag::WantsToSprint);
	bIsSliding        = Has(EPolarityMoveFlag::Sliding);
	bIsWallRunning    = Has(EPolarityMoveFlag::WallRunning);
	bWantsSlideOnLand = Has(EPolarityMoveFlag::WantsSlideOnLand);
	bIsGroundDashing  = Has(EPolarityMoveFlag::GroundDashing);
	bIsAirDashing     = Has(EPolarityMoveFlag::AirDashing);
	bIsRedirecting    = Has(EPolarityMoveFlag::AirDashRedirect);
	bIsMantling       = Has(EPolarityMoveFlag::Mantling);

	bMeleeLungeWanted       = Has(EPolarityMoveFlag::MeleeLunging);
	bMeleeLungeHasTarget    = Has(EPolarityMoveFlag::MeleeLungeHasTarget);
	bMeleeLungeHoming       = Has(EPolarityMoveFlag::MeleeLungeHoming);
	bMeleeLungeRestoreOnEnd = Has(EPolarityMoveFlag::MeleeLungeRestore);
	bMeleeDropKick          = Has(EPolarityMoveFlag::MeleeDropKick);
	bMeleeDropKickForward   = Has(EPolarityMoveFlag::MeleeDropKickForward);

	// bIsMeleeLunging is restored by PrepMoveFor around this call, not from the flags: it is the
	// state the move started in, and the flags carry the decision the move was made with. The gravity
	// that goes with it is plain component state and no part of the saved move, so it has to be put
	// back by hand or the replayed flight falls while the original one did not.
	SyncMeleeLungeGravity();
}

// ==================== Client move on the wire ====================

void FCharacterNetworkMoveData_Polarity::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove,
	ENetworkMoveType MoveType)
{
	Super::ClientFillNetworkMoveData(ClientMove, MoveType);

	// The engine only ever allocates our move type here (see FNetworkPredictionData_Client_Polarity).
	const FSavedMove_Polarity& PolarityMove = static_cast<const FSavedMove_Polarity&>(ClientMove);
	PolarityFlags         = PolarityMove.SavedPolarityFlags;
	MeleeLungeTarget      = PolarityMove.SavedMeleeLungeTarget;
	MeleeLungeTargetActor = PolarityMove.SavedMeleeLungeTargetActor.Get();
}

bool FCharacterNetworkMoveData_Polarity::Serialize(UCharacterMovementComponent& CharacterMovement,
	FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	Super::Serialize(CharacterMovement, Ar, PackageMap, MoveType);

	// SerializeOptionalValue writes a single bit when the value is the default, which is the usual
	// case: a character that is walking normally has none of these set and pays one bit per move.
	// Widening the flags from a byte to a word costs nothing while nothing is happening, and one
	// extra byte only on the moves that actually carry a decision.
	SerializeOptionalValue<uint16>(Ar.IsSaving(), Ar, PolarityFlags, 0);

	// The lunge destination rides along only on the moves that have one, and the homing flag above is
	// its presence bit — so it costs nothing at all until a swing is actually flying at somebody, and
	// a quantised position (1/10 cm, the same precision the engine sends the character's own location
	// at) for the handful of moves that lasts.
	if ((PolarityFlags & static_cast<uint16>(EPolarityMoveFlag::MeleeLungeHoming)) != 0)
	{
		bool bLocalSuccess = true;
		MeleeLungeTarget.NetSerialize(Ar, PackageMap, bLocalSuccess);

		// And who it is. Same presence bit, and the same encoding the engine uses for MovementBase.
		SerializeOptionalValue<TObjectPtr<UObject>>(Ar.IsSaving(), Ar, MeleeLungeTargetActor, nullptr);
	}
	else if (Ar.IsLoading())
	{
		MeleeLungeTarget = FVector::ZeroVector;
		MeleeLungeTargetActor = nullptr;
	}

	return !Ar.IsError();
}

void UApexMovementComponent::ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData)
{
	// Only stash it here. The container guarantees the type, since we handed the engine our own, but
	// this is too early to act on: the acceleration for this move has not been set yet, and wallrun
	// entry and dash direction both read it. MoveAutonomous below applies it at the right moment.
	const FCharacterNetworkMoveData_Polarity& PolarityData =
		static_cast<const FCharacterNetworkMoveData_Polarity&>(MoveData);
	PendingPolarityFlags          = PolarityData.PolarityFlags;
	PendingMeleeLungeTarget       = PolarityData.MeleeLungeTarget;
	PendingMeleeLungeTargetActor  = Cast<AActor>(PolarityData.MeleeLungeTargetActor.Get());

	Super::ServerMove_PerformMovement(MoveData);
}

void UApexMovementComponent::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags,
	const FVector& NewAccel)
{
	// The engine's own order inside MoveAutonomous is: unpack flags, resolve the jump, THEN set
	// Acceleration from the move, then simulate. Anything that reads the input direction therefore
	// cannot run with the flags. Our decisions do read it, so the acceleration is established first
	// and applied again by Super a moment later, which is harmless because it is the same value.
	//
	// Authority only. A client replaying its own moves also lands here, but it restores this state
	// from the saved move in PrepMoveFor, and a stale byte from the last server move would undo it.
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_Authority)
	{
		Acceleration = ConstrainInputAcceleration(NewAccel);
		Acceleration = Acceleration.GetClampedToMaxSize(GetMaxAcceleration());

		// The destination goes in before the flags: ApplyPolarityMoveFlags starts the lunge, and a
		// lunge that starts against last move's target flies at where the enemy used to be.
		MeleeLungeTarget      = PendingMeleeLungeTarget;
		MeleeLungeTargetActor = PendingMeleeLungeTargetActor;
		ApplyPolarityMoveFlags(PendingPolarityFlags);
	}

	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}


void UApexMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// SimulatedOnly: the owner already knows (it decided), and the server learns through the saved
	// move. This copy exists purely for the machines that only watch this character.
	DOREPLIFETIME_CONDITION(UApexMovementComponent, bWantsToSprint, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(UApexMovementComponent, bIsSliding, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(UApexMovementComponent, bIsWallRunning, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(UApexMovementComponent, bIsMantling, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(UApexMovementComponent, bIsAirDashing, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(UApexMovementComponent, bIsGroundDashing, COND_SimulatedOnly);
}

FNetworkPredictionData_Client* UApexMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UApexMovementComponent* MutableThis = const_cast<UApexMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Polarity(*this);
	}
	return ClientPredictionData;
}

// ==================== FSavedMove_Polarity ====================

FSavedMove_Polarity::FSavedMove_Polarity()
	: SavedPolarityFlags(0)
	, SavedMeleeLungeTarget(FVector::ZeroVector)
	, SavedMeleeLungeStartVelocity(FVector::ZeroVector)
	, bSavedMeleeLunging(0)
	, bSavedMeleeLungeGravityOff(0)
	, bSavedJumpHeld(0)
	, SavedSlideFatigueCounter(0)
	// Declaration order from here down, or the compiler warns that the list lies about what runs first.
	, SavedWallRunElapsedTime(0.0f)
	, SavedWallRunNormal(FVector::ZeroVector)
	, SavedWallRunDirection(FVector::ZeroVector)
	, SavedWallRunEntrySpeed(0.0f)
	, SavedWallRunPeakSpeed(0.0f)
	, SavedWallRunCurrentSpeed(0.0f)
	, SavedWallRunSide(EWallSide::None)
	, SavedSlideFatigueDecayTimer(0.0f)
	, SavedSlideBoostCooldown(0.0f)
	, SavedSlideCooldown(0.0f)
	, SavedJumpHoldTimeRemaining(0.0f)
	, SavedCurrentJumpCount(0)
{
}

void FSavedMove_Polarity::Clear()
{
	Super::Clear();
	SavedPolarityFlags = 0;
	SavedMeleeLungeTarget = FVector::ZeroVector;
	SavedMeleeLungeTargetActor = nullptr;
	SavedMeleeLungeStartVelocity = FVector::ZeroVector;
	bSavedMeleeLunging = 0;
	bSavedMeleeLungeGravityOff = 0;
	bSavedJumpHeld = 0;
	SavedSlideFatigueCounter = 0;
	SavedSlideFatigueDecayTimer = 0.0f;
	SavedSlideBoostCooldown = 0.0f;
	SavedSlideCooldown = 0.0f;
	SavedJumpHoldTimeRemaining = 0.0f;
	SavedCurrentJumpCount = 0;
	SavedWallRunElapsedTime = 0.0f;
	SavedWallRunNormal = FVector::ZeroVector;
	SavedWallRunDirection = FVector::ZeroVector;
	SavedWallRunEntrySpeed = 0.0f;
	SavedWallRunPeakSpeed = 0.0f;
	SavedWallRunCurrentSpeed = 0.0f;
	SavedWallRunSide = EWallSide::None;
}


bool FSavedMove_Polarity::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	// Two moves may only be merged into one if they were simulated the same way. Merging a
	// sprinting move with a walking one would send the server a single move at one speed for a
	// stretch the client covered at two.
	const FSavedMove_Polarity* NewPolarityMove = static_cast<const FSavedMove_Polarity*>(NewMove.Get());
	if (NewPolarityMove && SavedPolarityFlags != NewPolarityMove->SavedPolarityFlags)
	{
		return false;
	}
	// The lunge destination moves with the enemy, so two lunging moves are only the same move if they
	// were aimed at the same place. Merging them would send the server one flight to the newer target
	// for a stretch the client flew in two directions.
	if (NewPolarityMove && !SavedMeleeLungeTarget.Equals(NewPolarityMove->SavedMeleeLungeTarget, 0.1f))
	{
		return false;
	}
	return Super::CanCombineWith(NewMove, Character, MaxDelta);
}

void FSavedMove_Polarity::SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel,
	FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);

	if (const UApexMovementComponent* Apex = Cast<UApexMovementComponent>(Character->GetCharacterMovement()))
	{
		SavedPolarityFlags = Apex->PackPolarityMoveFlags();
		bSavedJumpHeld     = Apex->bJumpHeld ? 1 : 0;

		// Only worth sending while the flight is actually homing; otherwise it stays zero and the
		// serializer spends one bit on it. @see FCharacterNetworkMoveData_Polarity::Serialize
		SavedMeleeLungeTarget        = Apex->bMeleeLungeHoming ? Apex->MeleeLungeTarget : FVector::ZeroVector;
		SavedMeleeLungeTargetActor   = Apex->bMeleeLungeHoming ? Apex->MeleeLungeTargetActor : nullptr;
		SavedMeleeLungeStartVelocity = Apex->MeleeLungeStartVelocity;
		bSavedMeleeLunging           = Apex->bIsMeleeLunging ? 1 : 0;
		bSavedMeleeLungeGravityOff   = Apex->bMeleeLungeGravityOff ? 1 : 0;

		SavedSlideFatigueCounter    = Apex->SlideFatigueCounter;
		SavedSlideFatigueDecayTimer = Apex->SlideFatigueDecayTimer;
		SavedSlideBoostCooldown     = Apex->SlideBoostCooldownRemaining;
		SavedSlideCooldown          = Apex->SlideCooldownRemaining;
		SavedJumpHoldTimeRemaining  = Apex->JumpHoldTimeRemaining;
		SavedCurrentJumpCount       = Apex->CurrentJumpCount;

		SavedWallRunElapsedTime  = Apex->WallRunElapsedTime;
		SavedWallRunNormal       = Apex->WallRunNormal;
		SavedWallRunDirection    = Apex->WallRunDirection;
		SavedWallRunEntrySpeed   = Apex->WallRunEntrySpeed;
		SavedWallRunPeakSpeed    = Apex->WallRunPeakSpeed;
		SavedWallRunCurrentSpeed = Apex->WallRunCurrentSpeed;
		SavedWallRunSide         = Apex->WallRunSide;
	}
}

void FSavedMove_Polarity::PrepMoveFor(ACharacter* Character)
{
	Super::PrepMoveFor(Character);

	// Restore the component to the state this move was recorded in, so replaying it reproduces it.
	if (UApexMovementComponent* Apex = Cast<UApexMovementComponent>(Character->GetCharacterMovement()))
	{
		// A replay restores the state wholesale rather than replaying the transitions: the move was
		// already simulated once from exactly these values, so re-running StartSlide and friends
		// would double their entry effects.
		// The destination and the captured momentum go in before the flags: SyncMeleeLungeGravity at
		// the end of ApplyPolarityMoveFlagsForReplay reads the restored flags, and UpdateMeleeLunge
		// reads both of these on the very move being replayed.
		Apex->MeleeLungeTarget        = SavedMeleeLungeTarget;
		Apex->MeleeLungeTargetActor   = SavedMeleeLungeTargetActor;
		Apex->MeleeLungeStartVelocity = SavedMeleeLungeStartVelocity;
		Apex->bIsMeleeLunging         = bSavedMeleeLunging != 0;
		Apex->bMeleeLungeGravityOff   = bSavedMeleeLungeGravityOff != 0;
		Apex->ApplyPolarityMoveFlagsForReplay(SavedPolarityFlags);
		Apex->bJumpHeld = bSavedJumpHeld != 0;

		Apex->SlideFatigueCounter         = SavedSlideFatigueCounter;
		Apex->SlideFatigueDecayTimer      = SavedSlideFatigueDecayTimer;
		Apex->SlideBoostCooldownRemaining = SavedSlideBoostCooldown;
		Apex->SlideCooldownRemaining      = SavedSlideCooldown;
		Apex->JumpHoldTimeRemaining       = SavedJumpHoldTimeRemaining;
		Apex->CurrentJumpCount            = SavedCurrentJumpCount;

		Apex->WallRunElapsedTime  = SavedWallRunElapsedTime;
		Apex->WallRunNormal       = SavedWallRunNormal;
		Apex->WallRunDirection    = SavedWallRunDirection;
		Apex->WallRunEntrySpeed   = SavedWallRunEntrySpeed;
		Apex->WallRunPeakSpeed    = SavedWallRunPeakSpeed;
		Apex->WallRunCurrentSpeed = SavedWallRunCurrentSpeed;
		Apex->WallRunSide         = SavedWallRunSide;
	}
}

// ==================== FNetworkPredictionData_Client_Polarity ====================

FNetworkPredictionData_Client_Polarity::FNetworkPredictionData_Client_Polarity(const UCharacterMovementComponent& ClientMovement)
	: Super(ClientMovement)
{
}

FSavedMovePtr FNetworkPredictionData_Client_Polarity::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_Polarity());
}

void UApexMovementComponent::TryCrouchSlide()
{
	if (IsFalling() && !bIsWallRunning)
	{
		bWantsSlideOnLand = true;

		// Start tracking hold time for air crouch
		AirCrouchHoldTime = 0.0f;

		// Hold = crouch in air (handled in TickComponent). Air Dash has its own dedicated input.
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
	AirCrouchHoldTime = 0.0f;

	// End air crouch
	if (bIsCrouchedInAir)
	{
		bIsCrouchedInAir = false;
		StopCrouching();
	}

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

	// Disable native UE5 braking - all slide deceleration handled by UpdateSlide()
	GroundFriction = 0.0f;
	BrakingDecelerationWalking = 0.0f;

	if (SlideBoostCooldownRemaining <= 0.0f)
	{
		const float CurrentSpeed = Velocity.Size2D();
		const float MinBoost = bExternalSlideSpeedBurstOverride ? ExternalSlideMinSpeedBurst : MovementSettings->SlideMinSpeedBurst;
		const float MaxBoost = bExternalSlideSpeedBurstOverride ? ExternalSlideMaxSpeedBurst : MovementSettings->SlideMaxSpeedBurst;
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
	if (bIsSliding || bIsMantling || bIsWallRunning || SlideCooldownRemaining > 0.0f)
	{
		// Filter the log by [NET_DEBUG] to see why a landing did not turn into a slide. Both ends
		// print it, so a line appearing on one side only is itself the answer.
		UE_LOG(LogTemp, Warning,
			TEXT("[NET_DEBUG] %s StartSlideFromAir REFUSED: sliding=%d mantling=%d wallrun=%d cooldown=%.2f"),
			(GetOwnerRole() == ROLE_Authority ? TEXT("SERVER") : TEXT("CLIENT")), bIsSliding ? 1 : 0, bIsMantling ? 1 : 0, bIsWallRunning ? 1 : 0, SlideCooldownRemaining);
		return;
	}

	bIsSliding = true;
	SlideDuration = 0.0f;
	SlideDirection = Velocity.GetSafeNormal2D();

	// Disable native UE5 braking - all slide deceleration handled by UpdateSlide()
	GroundFriction = MovementSettings->SlideFriction;
	BrakingDecelerationWalking = 0.0f;

	const float CurrentSpeed = Velocity.Size2D();
	const float MinBoost = bExternalSlideSpeedBurstOverride ? ExternalSlideMinSpeedBurst : MovementSettings->SlideMinSpeedBurst;
	const float MaxBoost = bExternalSlideSpeedBurstOverride ? ExternalSlideMaxSpeedBurst : MovementSettings->SlideMaxSpeedBurst;
	const float MinStartSpeed = MovementSettings->SlideMinStartSpeed;

	float SpeedRatio = FMath::Clamp((CurrentSpeed - MinStartSpeed) / 500.0f, 0.0f, 1.0f);
	float BaseBoost = FMath::Lerp(MaxBoost, MinBoost, SpeedRatio);
	const float FallBoostMultiplier = FMath::Clamp(FallSpeed / 1000.0f, 0.1f, 0.5f);
	const float FallBoost = CurrentSpeed * FallBoostMultiplier;
	const float TotalBoost = FMath::Min(BaseBoost + FallBoost, MaxBoost);

	if (SlideDirection.IsNearlyZero())
	{
		SlideDirection = CharacterOwner ? CharacterOwner->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	}

	if (SlideBoostCooldownRemaining <= 0.0f)
	{
		// Apply fatigue to air slide boost (same as slidehop)
		const float FatigueMultiplier = (SlideFatigueCounter < 5)
			? FMath::Max(0.2f, 1.0f - SlideFatigueCounter * 0.15f)
			: 0.0f;
		Velocity += SlideDirection * TotalBoost * FatigueMultiplier;
		SlideBoostCooldownRemaining = MovementSettings->SlideboostCooldown;
	}
	UE_LOG(LogSlide, Warning, TEXT("=== SLIDE FROM AIR === Speed=%.1f, Boost=%.1f, Fatigue=%d slide cooldown = %.1f"), Velocity.Size2D(), TotalBoost, SlideFatigueCounter, SlideBoostCooldownRemaining);

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

	// bWantsToCrouch is the only thing that should be set here. The engine packs it into the saved
	// move itself, so the server and every client reach the same conclusion, and
	// UpdateCharacterStateBeforeMovement turns it into a real Crouch() with the encroachment check.
	//
	// bIsCrouched is deliberately NOT set by hand any more. It is a replicated property: writing it
	// directly ran the engine's crouch on watching machines (OnRep_IsCrouched) at the same time as
	// this component was resizing the capsule itself, and the two fought — that was the up-and-down
	// bobbing on observed characters. IsCrouching() now becomes true one movement tick later, which
	// is how every other UE character behaves.
	bWantsToCrouch = true;
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

	bWantsToCrouchSmooth = false;

	// Same as StartCrouching: clear the intent and let the engine run UnCrouch(), which does its
	// own fit check and restores the mesh offset. Nothing here touches bIsCrouched or the capsule.
	bWantsToCrouch = false;
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
	// Retired. This used to interpolate the capsule half height every frame and teleport the actor
	// to compensate, which is the wrong shape for a Character: the teleport had no collision check
	// (it pushed the capsule through floors and left characters hanging above them), it ran on
	// every copy including simulated proxies, where it fought network smoothing, and it never
	// compensated the mesh the way OnStartCrouch does.
	//
	// Crouching now goes through bWantsToCrouch, and the engine does the resize in one step with a
	// fit check, the base-location drop (bCrouchMaintainsBaseLocation) and the mesh offset. The
	// smooth *look* is a camera/mesh concern and lives in APolarityCharacter::AccumulateFirstPersonPose.
	//
	// Kept as an empty body rather than deleted so the declaration in the header stays valid; the
	// header is untouched by this change.
}

void UApexMovementComponent::UpdateSlide(float DeltaTime)
{
	const float SlideMinSpeed = MovementSettings->SlideMinSpeed;
	const float SlideFlatDecel = MovementSettings->SlideFlatDeceleration;
	const float SlideUphillDecel = MovementSettings->SlideUphillDeceleration;
	const float SlideDownhillDecel = MovementSettings->SlideSlopeAcceleration;

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

	// Deceleration (single slope system: uphill adds decel, downhill reduces decel)
	const float SlopeAngle = GetSlopeAngle(); // positive = uphill, negative = downhill

	FVector HorizontalVel = FVector(Velocity.X, Velocity.Y, 0.0f);
	float HorizontalSpeed = HorizontalVel.Size();

	if (HorizontalSpeed > 0.0f)
	{
		// Steering comes from Acceleration, not CurrentMoveInput. CurrentMoveInput is written by the
		// local input handler, so on the server a client's character always reads "no input" and its
		// slide goes straight while the client's curves away. Acceleration is already the same
		// direction in world space (input rotated by the control rotation), and it travels with the
		// move. Its length is divided by the max to get back the 0..1 the threshold is written in.
		FVector CurrentDir = HorizontalVel.GetSafeNormal();
		const float InputStrength = Acceleration.Size2D() / FMath::Max(GetMaxAcceleration(), KINDA_SMALL_NUMBER);
		if (MovementSettings->SlideSteeringResponse > 0.0f
			&& InputStrength >= MovementSettings->SlideSteeringInputThreshold
			&& CharacterOwner)
		{
			const FVector WishDir = Acceleration.GetSafeNormal2D();

			if (!WishDir.IsNearlyZero())
			{
				const float DirectionDot = FVector::DotProduct(CurrentDir, WishDir);
				const float BackwardScale = DirectionDot < 0.0f
					? FMath::Clamp(MovementSettings->SlideBackwardSteeringScale, 0.0f, 1.0f)
					: 1.0f;
				const float Response = MovementSettings->SlideSteeringResponse * BackwardScale;

				if (Response > 0.0f)
				{
					CurrentDir = FMath::VInterpNormalRotationTo(CurrentDir, WishDir, DeltaTime, Response);
					HorizontalVel = CurrentDir * HorizontalSpeed;
				}
			}
		}

		float DecelAmount = SlideFlatDecel * DeltaTime;

		if (SlopeAngle > 3.0f)
		{
			// Uphill: extra deceleration
			float SlopeMultiplier = SlopeAngle / 45.0f;
			DecelAmount += SlideUphillDecel * SlopeMultiplier * DeltaTime;
		}
		else if (SlopeAngle < -3.0f)
		{
			// Downhill: reduce deceleration (can go negative = acceleration)
			float SlopeMultiplier = FMath::Abs(SlopeAngle) / 45.0f;
			DecelAmount -= SlideDownhillDecel * SlopeMultiplier * DeltaTime;
		}

		float NewSpeed = FMath::Max(HorizontalSpeed - DecelAmount, 0.0f);
		Velocity.X = CurrentDir.X * NewSpeed;
		Velocity.Y = CurrentDir.Y * NewSpeed;
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

	if (bWallRunExternallyDisabled)
	{
		return false;
	}

	if (bIsSliding || bIsMantling || bIsWallRunning || bIsCrouchedInAir)
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

	// Acceleration, not CurrentMoveInput: this runs inside the simulation now, and on the server a
	// client's pawn always reads "no input" from the local handler, so the wallrun the client
	// started would never begin here and the client would be corrected off the wall.
	if (Acceleration.SizeSquared2D() < KINDA_SMALL_NUMBER)
	{
		return; // No input = no wallrun
	}

	FHitResult LeftHit, RightHit;
	bool bLeftWall = TraceForWall(EWallSide::Left, LeftHit);
	bool bRightWall = TraceForWall(EWallSide::Right, RightHit);

	// Acceleration already IS the input in world space, and unlike CurrentMoveInput it travels with
	// the move, so the server can answer this too.
	const FVector InputWorldDir = Acceleration.GetSafeNormal2D();

	auto CanStartOnWall = [&](const FHitResult& WallHit) -> bool
	{
		// Calculate wall direction
		FVector AlongWall = FVector::CrossProduct(WallHit.Normal, FVector::UpVector);
		if (FVector::DotProduct(Velocity, AlongWall) < 0)
		{
			AlongWall = -AlongWall;
		}
		// Check if input is roughly parallel to wall
		float InputDot = FVector::DotProduct(InputWorldDir, AlongWall);
		return InputDot >= MovementSettings->WallRunInputThreshold;
	};

	if (bLeftWall && bRightWall)
	{
		bool bCanLeft = CanStartOnWall(LeftHit);
		bool bCanRight = CanStartOnWall(RightHit);

		if (bCanLeft && bCanRight)
		{
			// Pick closer wall
			float LeftDot = FVector::DotProduct(Velocity.GetSafeNormal2D(), -LeftHit.Normal);
			float RightDot = FVector::DotProduct(Velocity.GetSafeNormal2D(), -RightHit.Normal);
			if (LeftDot > RightDot)
				StartWallRun(LeftHit, EWallSide::Left);
			else
				StartWallRun(RightHit, EWallSide::Right);
		}
		else if (bCanLeft)
		{
			StartWallRun(LeftHit, EWallSide::Left);
		}
		else if (bCanRight)
		{
			StartWallRun(RightHit, EWallSide::Right);
		}
	}
	else if (bLeftWall && CanStartOnWall(LeftHit))
	{
		StartWallRun(LeftHit, EWallSide::Left);
	}
	else if (bRightWall && CanStartOnWall(RightHit))
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

	if (Hit.GetActor() && Hit.GetActor()->ActorHasTag(FName("NoWallRun")))
	{
		return false;
	}

	if (Hit.GetActor() && (Hit.GetActor()->ActorHasTag(FName("SportsBall")) || Hit.GetActor()->ActorHasTag(FName("BasketballBall"))))
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

	// Both ends print this. A START on the client with no matching START on the server means the
	// server could not find the wall, and the correction that follows is what pulls the character
	// off it; repeated pairs mean it is entering and leaving every few frames.
	UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s WallRun START side=%d speed=%.0f"),
		(GetOwnerRole() == ROLE_Authority ? TEXT("SERVER") : TEXT("CLIENT")), static_cast<int32>(Side), Velocity.Size2D());

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

	// Calculate parallel speed
	const float ParallelSpeed = FMath::Abs(FVector::DotProduct(Velocity, WallDirection));

	// Check if speed is too low for wallrun
	if (ParallelSpeed < MovementSettings->WallRunMinSpeed)
	{
		return;
	}

	bIsWallRunning = true;
	WallRunSide = Side;
	WallRunNormal = WallHit.Normal;
	WallRunDirection = WallDirection;

	// Apply wallrun gravity scale
	GravityScale = MovementSettings->WallRunGravityScale;

	// Titanfall 2 style: track elapsed time, entry speed, calculate peak
	WallRunElapsedTime = 0.0f;
	WallRunEntrySpeed = ParallelSpeed;
	//WallRunPeakSpeed = ParallelSpeed * MovementSettings->WallRunPeakSpeedMultiplier;
	WallRunPeakSpeed = FMath::Max(MovementSettings->WallRunSpeed, ParallelSpeed);
	WallRunCurrentSpeed = ParallelSpeed;
	WallRunDistanceTraveled = 0.0f;
	WallRunHeadbobRoll = 0.0f;

	// Reset jump count and wallrun end reason
	CurrentJumpCount = 0;
	LastWallRunEndReason = EWallRunEndReason::None;
	LastWallRunActor = WallHit.GetActor();

	// Apply smaller capsule (Titanfall 2 style - NO TILT)
	ApplyWallRunCapsule();

	UE_LOG(LogWallRun, Warning, TEXT("=== WALLRUN STARTED === EntrySpeed=%.1f, PeakSpeed=%.1f, Side=%s"),
		WallRunEntrySpeed, WallRunPeakSpeed, Side == EWallSide::Left ? TEXT("Left") : TEXT("Right"));

	OnWallRunChanged.Broadcast(true, Side);
	OnWallrunStarted.Broadcast(Side);
}

void UApexMovementComponent::SetWallRunExternallyDisabled(bool bDisabled)
{
	bWallRunExternallyDisabled = bDisabled;

	// If we just got disabled while a wallrun is in progress, kill it immediately.
	if (bDisabled && bIsWallRunning)
	{
		EndWallRun(EWallRunEndReason::LostWall);
	}
}

void UApexMovementComponent::SetRunLaunchActive(bool bActive)
{
	bRunLaunchActive = bActive;

	// Entering the launch: cancel any contextual air state so the toss arc is clean.
	if (bActive && bIsWallRunning)
	{
		EndWallRun(EWallRunEndReason::LostWall);
	}
}

void UApexMovementComponent::EndWallRun(EWallRunEndReason Reason)
{
	if (!bIsWallRunning)
	{
		return;
	}

	UE_LOG(LogWallRun, Log, TEXT("WALLRUN ENDED: FinalSpeed=%.1f, Reason=%d"), Velocity.Size2D(), static_cast<int32>(Reason));
	UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s WallRun END reason=%d elapsed=%.2f"),
		(GetOwnerRole() == ROLE_Authority ? TEXT("SERVER") : TEXT("CLIENT")), static_cast<int32>(Reason), WallRunElapsedTime);

	bIsWallRunning = false;
	WallRunSide = EWallSide::None;
	LastWallRunEndReason = Reason;

	// Restore normal capsule
	RestoreWallRunCapsule();

	if (MovementSettings)
	{
		// Restore default gravity
		GravityScale = MovementSettings->DefaultGravityScale;
		WallRunSameWallCooldown = MovementSettings->WallRunSameWallCooldown;
	}

	// After any wallrun, allow exactly one air jump (treated as double jump)
	const int32 MaxJumps = MovementSettings ? MovementSettings->MaxJumpCount : 2;
	CurrentJumpCount = FMath::Max(CurrentJumpCount, MaxJumps - 1);

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

	// Update elapsed time
	WallRunElapsedTime += DeltaTime;

	// Time limit check
	if (WallRunElapsedTime >= MovementSettings->WallRunMaxDuration)
	{
		EndWallRun(EWallRunEndReason::TimeExpired);
		return;
	}

	// Still pushing along the wall?
	//
	// This has to read Acceleration and not CurrentMoveInput. The local input handler writes
	// CurrentMoveInput, so on the server someone else's pawn always reads "nothing held" and this
	// branch dropped it off the wall on the very first frame. The client kept running, the server
	// kept ending, the flag on the next move started it again, and the result was a character
	// stuttering along the wall and then falling off it.
	const FVector InputWorldDir = Acceleration.GetSafeNormal2D();
	if (InputWorldDir.IsNearlyZero())
	{
		// No input = end wallrun
		EndWallRun(EWallRunEndReason::LostWall);
		return;
	}

	const float InputDot = FVector::DotProduct(InputWorldDir, WallRunDirection);
	if (InputDot < MovementSettings->WallRunInputThreshold)
	{
		EndWallRun(EWallRunEndReason::LostWall);
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

	// ===== TITANFALL 2 SPEED CURVE: Acceleration -> Peak -> Deceleration =====
	const float PeakTime = MovementSettings->WallRunPeakTime;

	if (WallRunElapsedTime < PeakTime)
	{
		// Phase 1: Acceleration towards peak speed
		float AccelProgress = WallRunElapsedTime / PeakTime;
		// Smooth acceleration curve (ease out)
		AccelProgress = 1.0f - FMath::Pow(1.0f - AccelProgress, 2.0f);
		WallRunCurrentSpeed = FMath::Lerp(WallRunEntrySpeed, WallRunPeakSpeed, AccelProgress);
	}
	else
	{
		// Phase 2: Deceleration from peak
		WallRunCurrentSpeed -= MovementSettings->WallRunDeceleration * DeltaTime;
	}

	// End if too slow
	if (WallRunCurrentSpeed < MovementSettings->WallRunEndSpeed)
	{
		UE_LOG(LogWallRun, Warning, TEXT("Wallrun ended: speed %.1f < min %.1f"),
			WallRunCurrentSpeed, MovementSettings->WallRunEndSpeed);
		EndWallRun(EWallRunEndReason::LostWall);
		return;
	}

	// Apply velocity along wall direction
	Velocity = WallRunDirection * WallRunCurrentSpeed;

	// NO GRAVITY during wallrun
	Velocity.Z = 0.0f;

	// Stick to wall
	FVector ToWall = -WallRunNormal * 50.0f;
	Velocity += ToWall * DeltaTime;

	// ===== HEADBOB =====
	// Accumulate distance traveled
	WallRunDistanceTraveled += WallRunCurrentSpeed * DeltaTime;

	// Calculate bob phase (0 to 2π per step)
	const float StepLength = MovementSettings->WallRunHeadbobStepLength;
	const float BobPhase = (WallRunDistanceTraveled / StepLength) * 2.0f * PI;

	// Calculate amplitude based on speed (0 at EndSpeed, max at PeakSpeed)
	const float SpeedRange = WallRunPeakSpeed - MovementSettings->WallRunEndSpeed;
	const float SpeedRatio = FMath::Clamp(
		(WallRunCurrentSpeed - MovementSettings->WallRunEndSpeed) / SpeedRange,
		0.0f, 1.0f
	);
	const float MaxAmplitude = MovementSettings->WallRunHeadbobRollAmount;
	const float CurrentAmplitude = MaxAmplitude * SpeedRatio;

	// Calculate headbob roll
	WallRunHeadbobRoll = FMath::Sin(BobPhase) * CurrentAmplitude;

	UE_LOG(LogWallRun, Log, TEXT("WALLRUN: Speed=%.1f, Elapsed=%.2f, Phase=%s, Headbob=%.2f"),
		WallRunCurrentSpeed, WallRunElapsedTime,
		WallRunElapsedTime < PeakTime ? TEXT("Accel") : TEXT("Decel"),
		WallRunHeadbobRoll);
}

void UApexMovementComponent::UpdateWallRunCameraTilt(float DeltaTime)
{
	if (!MovementSettings)
	{
		CurrentWallRunCameraRoll = 0.0f;
		WallRunBaseCameraRoll = 0.0f;
		WallRunHeadbobRoll = 0.0f;
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
	else
	{
		// Not wallrunning - reset headbob immediately
		WallRunHeadbobRoll = 0.0f;
	}

	// Interpolate base camera roll (without headbob)
	WallRunBaseCameraRoll = FMath::FInterpTo(
		WallRunBaseCameraRoll,
		TargetCameraRoll,
		DeltaTime,
		MovementSettings->WallRunCameraTiltSpeed
	);

	// Final camera roll = base + headbob
	CurrentWallRunCameraRoll = WallRunBaseCameraRoll + WallRunHeadbobRoll;

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

	// Block for mantling and wallrun
	if (bIsMantling || bIsWallRunning)
	{
		return false;
	}

	// Wall bounce ONLY works in these cases:
	// 1. Sliding (ground crouch with high speed)
	// 2. Air crouch (bIsCrouchedInAir = holding crouch in air)
	if (!bIsSliding && !bIsCrouchedInAir)
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

	// Sweep forward in velocity direction (more reliable than line trace)
	const FVector VelDir = Velocity.GetSafeNormal();
	if (VelDir.IsNearlyZero())
	{
		return;
	}

	UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float TraceDistance = CapsuleRadius + 50.0f;
	const FVector Start = CharacterOwner->GetActorLocation();
	const FVector End = Start + VelDir * TraceDistance;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(CharacterOwner);

	FHitResult Hit;
	// Use sphere sweep for more reliable detection
	const float SweepRadius = CapsuleRadius * 0.8f;
	if (!GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(SweepRadius), Params))
	{
		return;
	}

	if (Hit.GetActor() && (Hit.GetActor()->ActorHasTag(FName("SportsBall")) || Hit.GetActor()->ActorHasTag(FName("BasketballBall"))))
	{
		return;
	}

	// Check if wall is vertical enough
	if (FMath::Abs(Hit.Normal.Z) > 0.3f)
	{
		return;
	}

	// Wall bounce происходит всегда при приседе в воздухе (bIsCrouchedInAir) или скольжении (bIsSliding)
	// Никаких проверок на угол или скорость
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
#if ENABLE_DRAW_DEBUG
		if (GEngine && IsFalling())
		{
			GEngine->AddOnScreenDebugMessage(8799, 0.0f, FColor::Orange,
				FString::Printf(TEXT("MANTLE BLOCKED: Settings=%d Mantling=%d Sliding=%d WallRun=%d Falling=%d"),
					MovementSettings ? 1 : 0, bIsMantling, bIsSliding, bIsWallRunning, IsFalling()));
		}
#endif
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

#if ENABLE_DRAW_DEBUG
	if (GEngine)
	{
		const float MantleDist = FVector::Dist(MantleStartLocation, MantleTargetLocation);
		GEngine->AddOnScreenDebugMessage(8804, 2.0f, FColor::Green,
			FString::Printf(TEXT("MANTLE START! From Z=%.0f → To Z=%.0f (dist=%.0f, duration=%.2fs)"),
				MantleStartLocation.Z, MantleTargetLocation.Z, MantleDist, MovementSettings->MantleDuration));
	}
	DrawDebugLine(GetWorld(), MantleStartLocation, MantleTargetLocation, FColor::Green, false, 2.0f, 0, 3.0f);
#endif

	// Broadcast mantle started event
	OnMantleStarted.Broadcast();
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

		// Broadcast mantle ended event
		OnMantleEnded.Broadcast();
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

	const FVector CharLoc = CharacterOwner->GetActorLocation();
	const FVector Start = CharLoc + FVector(0, 0, 50.0f);
	const FVector Forward = CharacterOwner->GetActorForwardVector();
	const FVector End = Start + Forward * MovementSettings->MantleTraceDistance;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(CharacterOwner);

#if ENABLE_DRAW_DEBUG
	// Debug: wall trace (yellow = searching for wall)
	DrawDebugLine(GetWorld(), Start, End, FColor::Yellow, false, 0.5f, 0, 2.0f);
	DrawDebugPoint(GetWorld(), Start, 8.0f, FColor::Yellow, false, 0.5f);
#endif

	FHitResult WallHit;
	if (!GetWorld()->LineTraceSingleByChannel(WallHit, Start, End, ECC_Visibility, Params))
	{
#if ENABLE_DRAW_DEBUG
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(8800, 0.5f, FColor::Red,
				FString::Printf(TEXT("MANTLE: No wall found (trace dist=%.0f, from Z=%.0f)"),
					MovementSettings->MantleTraceDistance, Start.Z));
		}
#endif
		return false;
	}

#if ENABLE_DRAW_DEBUG
	// Debug: wall hit point (green)
	DrawDebugPoint(GetWorld(), WallHit.Location, 12.0f, FColor::Green, false, 0.5f);
#endif

	const FVector LedgeTraceStart = WallHit.Location + Forward * 10.0f + FVector(0, 0, MovementSettings->MantleReachHeight);
	const FVector LedgeTraceEnd = WallHit.Location + Forward * 10.0f;

#if ENABLE_DRAW_DEBUG
	// Debug: ledge trace (cyan = searching for ledge surface)
	DrawDebugLine(GetWorld(), LedgeTraceStart, LedgeTraceEnd, FColor::Cyan, false, 0.5f, 0, 2.0f);
	DrawDebugPoint(GetWorld(), LedgeTraceStart, 8.0f, FColor::Cyan, false, 0.5f);

	const float WallHitHeight = WallHit.Location.Z;
	const float CharFeet = CharLoc.Z - CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float LedgeSearchTop = WallHitHeight + MovementSettings->MantleReachHeight;

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(8801, 0.0f, FColor::Yellow,
			FString::Printf(TEXT("MANTLE: Wall hit at Z=%.0f | Feet at Z=%.0f | Search top Z=%.0f | Reach=%.0f"),
				WallHitHeight, CharFeet, LedgeSearchTop, MovementSettings->MantleReachHeight));
	}
#endif

	if (!GetWorld()->LineTraceSingleByChannel(OutHit, LedgeTraceStart, LedgeTraceEnd, ECC_Visibility, Params))
	{
#if ENABLE_DRAW_DEBUG
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(8802, 0.5f, FColor::Red,
				TEXT("MANTLE: No ledge surface found (trace went through - wall too tall for MantleReachHeight?)"));
		}
#endif
		return false;
	}

#if ENABLE_DRAW_DEBUG
	// Debug: ledge hit point
	DrawDebugPoint(GetWorld(), OutHit.Location, 12.0f, FColor::Blue, false, 0.5f);

	const float LedgeHeight = OutHit.Location.Z;
	const float CharFeetZ = CharLoc.Z - CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float HeightAboveFeet = LedgeHeight - CharFeetZ;

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(8803, 0.0f, FColor::Cyan,
			FString::Printf(TEXT("MANTLE: Ledge at Z=%.0f | %.0f UU above feet | Normal.Z=%.2f %s"),
				LedgeHeight, HeightAboveFeet, OutHit.Normal.Z,
				OutHit.Normal.Z > 0.7f ? TEXT("OK") : TEXT("TOO STEEP!")));
	}

	// Draw the mantle target location
	const FVector TargetLoc = OutHit.Location + FVector(0, 0, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	DrawDebugSphere(GetWorld(), TargetLoc, 20.0f, 8, OutHit.Normal.Z > 0.7f ? FColor::Green : FColor::Red, false, 1.0f);
#endif

	return OutHit.Normal.Z > 0.7f;
}

// ==================== Air Movement ====================

bool UApexMovementComponent::IsAccelerationForward() const
{
	const FVector AccelDir = Acceleration.GetSafeNormal2D();
	if (AccelDir.IsNearlyZero())
	{
		return false;
	}

	const AController* Controller = CharacterOwner ? CharacterOwner->GetController() : nullptr;
	if (!Controller)
	{
		// No controller means this is a simulated proxy, which never runs this path anyway.
		return IsForwardHeld();
	}

	// 0.7 is cos(45deg): W alone and the two W+strafe diagonals pass, pure strafe and back do not,
	// which is the same set CurrentMoveInput.Y > 0.5 accepted.
	const FVector ViewDir = Controller->GetControlRotation().Vector().GetSafeNormal2D();
	return FVector::DotProduct(AccelDir, ViewDir) > 0.7f;
}

// ==================== Melee lunge ====================

void UApexMovementComponent::SetMeleeLungeTuning(float MaxSpeed, float MomentumRatio, bool bDisableGravity,
	float DropKickSpeed)
{
	MeleeLungeMaxSpeed          = MaxSpeed;
	MeleeLungeMomentumRatio     = MomentumRatio;
	bMeleeLungeDisablesGravity  = bDisableGravity;
	MeleeDropKickSpeed          = DropKickSpeed;
}

void UApexMovementComponent::SetMeleeLungeIntent(bool bLunging, bool bHasTarget, bool bHoming,
	const FVector& InTarget, AActor* InTargetActor, bool bDropKick, bool bDropKickForwardHeld)
{
	// Assigned only while the flight is on, and deliberately NOT cleared when it stops: EndMeleeLunge
	// runs one move later and both of these are what it reads. Clearing them here would have every
	// dropkick end as though it were a lunge, which is the same mistake the gravity ownership flag
	// was written to avoid.
	if (bLunging)
	{
		bMeleeDropKick        = bDropKick;
		bMeleeDropKickForward = bDropKickForwardHeld;
	}

	// Note what is NOT touched here: bMeleeLungeRestoreOnEnd, which the melee component sets on its
	// own when it decides the swing missed, and bMeleeLungeGravityOff, which belongs to the flight
	// that turned gravity off and is cleared by the flight that turns it back on.
	bMeleeLungeWanted    = bLunging;
	bMeleeLungeHasTarget = bLunging && bHasTarget;
	bMeleeLungeHoming    = bLunging && bHoming;

	// Kept at zero unless the flight is homing, so the saved move and the wire both stay empty for a
	// lunge that is only holding momentum. @see FCharacterNetworkMoveData_Polarity::Serialize
	MeleeLungeTarget = bMeleeLungeHoming ? InTarget : FVector::ZeroVector;

	// The ACTOR is only ever assigned, never cleared here. EndMeleeLunge runs a move later and needs
	// it to take the move-collision ignore back off; clearing it with the intent meant the ignore was
	// added and never removed, so two players who had lunged at each other once stopped colliding for
	// the rest of the round. Third time this exact shape has bitten: gravity ownership, the dropkick
	// flags, and now this.
	if (bMeleeLungeHoming && InTargetActor)
	{
		MeleeLungeTargetActor = InTargetActor;
	}
}

void UApexMovementComponent::SyncMeleeLungeGravity()
{
	// Deliberately one-way: this only ever turns gravity OFF for a flight that owns it. The way back
	// is EndMeleeLunge, on the lunge's falling edge. A symmetric version that also restored the
	// default would fire on every replayed move and stomp whoever else had gravity parked at zero at
	// the time — UUpgrade_ChargedPunch does exactly that for its own flight.
	//
	// A wallrun owns gravity too for as long as it lasts (WallRunGravityScale), with EndWallRun
	// putting it back. Leave it alone rather than fight over the same field.
	if (bMeleeLungeGravityOff && !bIsWallRunning)
	{
		GravityScale = 0.0f;
	}
}

void UApexMovementComponent::StartMeleeLunge()
{
	// The speed the swing began at, and the only thing the whole momentum system is built on: a
	// punch thrown at 2000 u/s has to come out the other side still doing 2000 u/s. Measured, not
	// received — by this move both ends have simulated everything before it the same way.
	MeleeLungeStartVelocity = Velocity;

	// Gravity off for a flight with a target, so the arc is a straight line at the enemy instead of
	// a drop. A lunge with nobody to fly at keeps its gravity and just holds its speed. Decided here
	// and here only — see bMeleeLungeGravityOff.
	bMeleeLungeGravityOff = bMeleeLungeHasTarget && bMeleeLungeDisablesGravity;
	SyncMeleeLungeGravity();

	// Stop colliding with whoever is being flown at, on BOTH ends.
	//
	// The flight parks the character inside the target's capsule on purpose — the stop distance is
	// measured from its centre and is shorter than the two radii together — so without this the
	// engine spends the whole arrival depenetrating them. This used to be done in the melee
	// component, which runs on the swinging machine only: the client passed through the enemy while
	// the server kept colliding with it, and they disagreed about where the character finished.
	SetMeleeLungeTargetIgnored(true);

	// Edge only, never per frame, and never from a replay. A correction re-runs this move from the
	// corrected state, which legitimately re-measures the entry speed and converges on the server's
	// number — printing every one of those made a healthy convergence (805, 846, 899 against the
	// server's 918) read like a desync and nearly cost a fix to code that was working.
	//
	// The pawn name is in there because the role is not enough to read this log. On a listen server
	// the host's OWN character and every client's character are both ROLE_Authority, so a host log
	// full of role=3 says nothing about whose swing it was. "own" marks the pawn this machine drives.
	if (!CharacterOwner || !CharacterOwner->bClientUpdating)
	{
	UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] MeleeLunge START %s role=%d own=%d dropkick=%d at=%s target=%d homing=%d startVel=%.0f to=(%.0f,%.0f,%.0f)"),
		*GetNameSafe(CharacterOwner),
		CharacterOwner ? (int32)CharacterOwner->GetLocalRole() : -1,
		(CharacterOwner && CharacterOwner->IsLocallyControlled()) ? 1 : 0,
		bMeleeDropKick ? 1 : 0, *GetNameSafe(MeleeLungeTargetActor.Get()),
		bMeleeLungeHasTarget ? 1 : 0, bMeleeLungeHoming ? 1 : 0,
		MeleeLungeStartVelocity.Size(), MeleeLungeTarget.X, MeleeLungeTarget.Y, MeleeLungeTarget.Z);
	}
}

void UApexMovementComponent::EndMeleeLunge()
{
	// A drop kick always leaves with half the speed it dove at, along the way the character is facing.
	//
	// It used to read the player's input here and zero the velocity if they were not pushing forward.
	// That cannot survive a network: the server reads a remote pawn's input as nothing at all, so a
	// client's dropkick would have ended in a dead stop every time. The branch is gone rather than
	// networked, by the author's decision, and preserving the momentum is the half that matches the
	// rest of the melee system, whose stated rule is never to kill the player's momentum.
	if (bMeleeDropKick)
	{
		// Pushing forward as it ends carries you out of it at half the dive speed; letting go stops
		// you dead. The input itself cannot be read here — the server sees a remote pawn's input as
		// nothing — so the answer travelled as a flag decided on the machine that had it.
		if (bMeleeDropKickForward)
		{
			FVector ForwardDir = UpdatedComponent ? UpdatedComponent->GetForwardVector() : FVector::ZeroVector;
			ForwardDir.Z = 0.0f;
			if (ForwardDir.Normalize())
			{
				Velocity = ForwardDir * (MeleeDropKickSpeed * 0.5f);
			}
		}
		else
		{
			Velocity = FVector::ZeroVector;
		}

		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] DropKick exit %s role=%d forward=%d at %.0f u/s"),
			*GetNameSafe(CharacterOwner),
			CharacterOwner ? (int32)CharacterOwner->GetLocalRole() : -1,
			bMeleeDropKickForward ? 1 : 0, Velocity.Size());

		bMeleeDropKick        = false;
		bMeleeDropKickForward = false;
	}
	// A swing that missed gives the player their run back. Without this a whiff at full speed parks
	// the character at the end of the flight, which is the one thing the momentum system exists to
	// prevent. A swing that connected keeps whatever the flight left it with.
	else if (bMeleeLungeRestoreOnEnd)
	{
		FVector Restored = MeleeLungeStartVelocity * MeleeLungeMomentumRatio;
		if (IsFalling())
		{
			// Don't fight gravity on the way down: the fall speed is the current one, not the one
			// the swing started with.
			Restored.Z = Velocity.Z;
		}
		Velocity = Restored;
	}

	SetMeleeLungeTargetIgnored(false);

	// Gravity back. Only when this lunge is the one that took it away, and never over a wallrun,
	// which manages the same field for itself.
	if (bMeleeLungeGravityOff && !bIsWallRunning)
	{
		GravityScale = MovementSettings ? MovementSettings->DefaultGravityScale : 1.5f;
	}

	if (!CharacterOwner || !CharacterOwner->bClientUpdating)
	{
	UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] MeleeLunge END %s role=%d own=%d gravityWasOff=%d restored=%d vel=%.0f"),
		*GetNameSafe(CharacterOwner),
		CharacterOwner ? (int32)CharacterOwner->GetLocalRole() : -1,
		(CharacterOwner && CharacterOwner->IsLocallyControlled()) ? 1 : 0,
		bMeleeLungeGravityOff ? 1 : 0, bMeleeLungeRestoreOnEnd ? 1 : 0, Velocity.Size());
	}

	bMeleeLungeGravityOff = false;
	bMeleeLungeHasTarget  = false;
	bMeleeLungeHoming     = false;
	MeleeLungeTarget      = FVector::ZeroVector;
	MeleeLungeTargetActor = nullptr;
}

void UApexMovementComponent::SetMeleeLungeTargetIgnored(bool bIgnore)
{
	// Guarded by its own flag so the ignore is added once and removed once, by the same flight, even
	// though a replay can run these edges more than once.
	if (bIgnore == bMeleeLungeIgnoringTarget)
	{
		return;
	}

	AActor* Target = MeleeLungeTargetActor.Get();
	if (!Target || !UpdatedComponent)
	{
		// Nothing to ignore, or the target died mid-flight. Either way this side is not holding one.
		bMeleeLungeIgnoringTarget = false;
		return;
	}

	if (UPrimitiveComponent* OwnerRoot = Cast<UPrimitiveComponent>(UpdatedComponent))
	{
		OwnerRoot->IgnoreActorWhenMoving(Target, bIgnore);
	}
	if (UPrimitiveComponent* TargetRoot = Cast<UPrimitiveComponent>(Target->GetRootComponent()))
	{
		TargetRoot->IgnoreActorWhenMoving(GetOwner(), bIgnore);
	}

	bMeleeLungeIgnoringTarget = bIgnore;
}

void UApexMovementComponent::UpdateMeleeLunge(float DeltaSeconds)
{
	if (bMeleeDropKick)
	{
		// A dive, not a lunge. Constant speed at the target rather than closing speed, which is what
		// makes it read as dropping ON somebody instead of being pulled to them. Constant speed also
		// sidesteps the amplification the lunge had to be protected from: nothing here divides a
		// distance by delta time, so a small disagreement between two machines stays small.
		if (bMeleeLungeHoming)
		{
			const FVector ToTarget = MeleeLungeTarget - UpdatedComponent->GetComponentLocation();
			const FVector Direction = ToTarget.GetSafeNormal();
			Velocity = Direction * MeleeDropKickSpeed;
		}
		else
		{
			// Tracking stopped: the target was knocked away, or the hit already landed. The original
			// stopped dead here and this keeps that.
			Velocity = FVector::ZeroVector;
		}
		return;
	}

	if (bMeleeLungeHoming)
	{
		// Continuous proportional homing: speed is the distance still to cover this frame, capped at
		// the lunge speed. Far away that is full speed; close in the step shrinks to exactly close
		// the gap, which is what keeps a flight at a moving target (or a drone) from oscillating
		// between "fly" and "hold". Full 3D, so Z converges too and nothing is left over for the
		// moment gravity comes back on.
		//
		// "The gap" is measured to the edge of MeleeLungeArrivalRadius, not to the point itself. That
		// single subtraction is what makes this survive a network: dividing by delta time amplifies
		// any difference between two machines about sixtyfold, so aiming at a point meant the server
		// and the client could sit 7 cm and 1 cm away and come out at 440 u/s against 0. Aiming at a
		// sphere lets the speed reach zero smoothly at its edge, and the two ends only have to agree
		// to within its radius.
		const FVector ToTarget = MeleeLungeTarget - UpdatedComponent->GetComponentLocation();
		const float Distance = ToTarget.Size();
		const float Remaining = Distance - MeleeLungeArrivalRadius;

		if (Remaining > 0.0f)
		{
			const float SafeDt = FMath::Max(DeltaSeconds, 0.001f);
			const float StepSpeed = FMath::Min(MeleeLungeMaxSpeed, Remaining / SafeDt);
			Velocity = (ToTarget / Distance) * StepSpeed;
		}
		else
		{
			// Inside the radius. Gravity is off for a homing flight, so zeroing all three axes is
			// safe and leaves nothing to drift on.
			Velocity = FVector::ZeroVector;
		}
		return;
	}

	// Not homing: hold the speed the swing started with and let the axis that gravity owns keep
	// whatever it has.
	FVector Held = MeleeLungeStartVelocity * MeleeLungeMomentumRatio;
	if (!bMeleeLungeHasTarget)
	{
		// No target was ever acquired, so gravity was never turned off and Z is a real fall.
		Held.Z = Velocity.Z;
	}
	// With a target, Z stays the swing's starting Z: the homing that just stopped (a landed hit, a
	// target knocked away) leaves a frozen vertical speed behind, and holding THAT with gravity off
	// is the fly-away when you punch something from below.
	Velocity = Held;
}

void UApexMovementComponent::UpdateHeldByAlly(float DeltaSeconds)
{
	AShooterCharacter* Shooter = Cast<AShooterCharacter>(CharacterOwner);
	AShooterCharacter* Holder = Shooter ? Shooter->GetHeldByCharacter() : nullptr;

	// A carrier who died or left keeps their grip forever otherwise: the held player stays pinned to a
	// hold point that nothing updates, with no input of their own, and nobody left to throw them.
	// Only the authority may end it, because only the authority owns the property.
	if (Holder && Shooter->HasAuthority() && (Holder->IsDead() || !IsValid(Holder)))
	{
		Shooter->HeldByCharacter = nullptr;
		Holder = nullptr;
	}

	if (!Holder)
	{
		// Let go. Falling rather than walking, because the carry ends in the air far more often than
		// on the ground, and the engine sorts a landing out on its own.
		if (bIsHeldByAlly)
		{
			bIsHeldByAlly = false;
			SetMovementMode(MOVE_Falling);
		}
		return;
	}

	if (!bIsHeldByAlly)
	{
		bIsHeldByAlly = true;
		SetMovementMode(MOVE_Flying);
	}

	// Control is lost while carried, and it is discarded HERE rather than at the input layer so that
	// the server's replay of this move discards exactly the same thing. Blocking input on the owning
	// client alone would leave the server replaying a move that still had acceleration in it.
	Acceleration = FVector::ZeroVector;
	if (CharacterOwner)
	{
		CharacterOwner->bPressedJump = false;
	}

	// A spring to the hold point rather than a teleport onto it: a teleport ignores geometry and
	// would post a carried player through walls, while a velocity is still swept by the mover and
	// stops on what is in the way.
	const FVector HoldPoint = Holder->GetAllyHoldPoint();
	const FVector ToHold = HoldPoint - UpdatedComponent->GetComponentLocation();
	Velocity = (ToHold * AllyHoldSpringRate).GetClampedToMaxSize(AllyHoldMaxSpeed);
}

void UApexMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);

	// Being carried overrides every mechanic below it and returns: a player in someone's hands is not
	// wallrunning, dashing or sliding, and letting those run would fight the hold for the same Velocity.
	{
		const AShooterCharacter* Shooter = Cast<AShooterCharacter>(CharacterOwner);
		const bool bHeldNow = Shooter && Shooter->IsHeldByAlly();
		if (bHeldNow || bIsHeldByAlly)
		{
			UpdateHeldByAlly(DeltaSeconds);
			if (bHeldNow)
			{
				return;
			}
		}
	}

	// Exactly the chain that used to sit at the top of TickComponent, in the same order. It runs
	// here because the engine calls this from inside PerformMovement, before the move is integrated,
	// which puts it in the simulation the server replays and a corrected client re-runs.
	if (bIsMantling)
	{
		UpdateMantle(DeltaSeconds);
	}
	else if (bIsWallRunning)
	{
		UpdateWallRun(DeltaSeconds);
	}
	else if (bIsGroundDashing)
	{
		UpdateGroundDash(DeltaSeconds);
	}
	else if (bIsAirDashing)
	{
		if (bIsRedirecting)
		{
			UpdateAirDashRedirect(DeltaSeconds);
		}
		else
		{
			UpdateAirDash(DeltaSeconds);
		}
	}
	else if (IsFalling() && !bIsSliding)
	{
		// Wall bounce when pushing forward, or while balled up in the air.
		if (IsAccelerationForward() || bIsCrouchedInAir)
		{
			CheckForWallBounce();
		}

		if (!bIsWallRunning)
		{
			CheckForWallRun();
		}
	}

	// The melee lunge goes last, and outside the chain above, because it overrides Velocity outright
	// for as long as it lasts — the same thing it did from the melee component's tick, where it was
	// whichever component ticked last that won. Both edges are resolved here rather than where the
	// decision is made, so the momentum capture and the miss impulse happen on the same simulated
	// move on the owning client, on the server and in a replay.
	if (bMeleeLungeWanted != bIsMeleeLunging)
	{
		bIsMeleeLunging = bMeleeLungeWanted;
		if (bIsMeleeLunging)
		{
			StartMeleeLunge();
		}
		else
		{
			EndMeleeLunge();
		}
	}

	if (bIsMeleeLunging)
	{
		UpdateMeleeLunge(DeltaSeconds);
	}
}

void UApexMovementComponent::PhysFalling(float deltaTime, int32 Iterations)
{
	// Sliding, dashing and wall running each drive velocity themselves; the tick guarded against
	// them before and this keeps that.
	if (!bIsSliding && !bIsAirDashing && !bIsWallRunning)
	{
		ApplyAirStrafe(deltaTime);
	}

	if (bJumpHeld)
	{
		UpdateJumpHold(deltaTime);
	}

	Super::PhysFalling(deltaTime, Iterations);
}

void UApexMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	// Momentum only: these three states drive Velocity themselves and must not get the normal input
	// acceleration on top. The zeroing happens here rather than in GetMaxAcceleration so that
	// Acceleration keeps pointing where the player is pushing. See GetMaxAcceleration.
	if (bIsSliding || bIsWallRunning || bIsGroundDashing)
	{
		const FVector InputAcceleration = Acceleration;
		Acceleration = FVector::ZeroVector;
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		Acceleration = InputAcceleration;
		return;
	}

	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
}

void UApexMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);

	// Slide deceleration and steering. This used to sit after Super::TickComponent, which is after
	// the physics of the local frame but outside the simulated move: the server ran it on its own
	// tick for a client's character and a replay never re-ran it at all, so the two ends braked and
	// steered the slide differently. Here it runs once per simulated move on every machine.
	if (bIsSliding)
	{
		CheckForWallBounce();
		UpdateSlide(DeltaSeconds);
	}

	if (SlideCooldownRemaining > 0.0f)
	{
		SlideCooldownRemaining -= DeltaSeconds;
	}
	if (SlideBoostCooldownRemaining > 0.0f)
	{
		SlideBoostCooldownRemaining -= DeltaSeconds;
	}
	if (WallRunSameWallCooldown > 0.0f)
	{
		WallRunSameWallCooldown -= DeltaSeconds;
	}
	if (WallBounceCooldownRemaining > 0.0f)
	{
		WallBounceCooldownRemaining -= DeltaSeconds;
	}

	// The dash cooldowns also drive a HUD delegate, which must fire exactly once when the cooldown
	// actually runs out and not again on every replay of the same move.
	auto TickDashCooldown = [this, DeltaSeconds](float& Cooldown)
	{
		if (Cooldown > 0.0f)
		{
			Cooldown -= DeltaSeconds;
			if (Cooldown <= 0.0f)
			{
				Cooldown = 0.0f;
				OnDashStateChanged.Broadcast();
			}
		}
	};
	TickDashCooldown(AirDashCooldownRemaining);
	TickDashCooldown(GroundDashCooldownRemaining);

	// Slide fatigue decays a step a second while not sliding. It scales the slide jump boost, which
	// is why it counts here and not in the tick.
	if (!bIsSliding && SlideFatigueCounter > 0)
	{
		SlideFatigueDecayTimer += DeltaSeconds;
		if (SlideFatigueDecayTimer >= 1.0f)
		{
			SlideFatigueCounter--;
			SlideFatigueDecayTimer = 0.0f;
		}
	}
}

void UApexMovementComponent::ApplyAirStrafe(float DeltaTime)
{
#if WITH_EDITOR
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9995, 0.0f, FColor::Red,
			FString::Printf(TEXT("ApplyAirStrafe: Settings=%d, AirStrafeMult=%.2f"),
				MovementSettings ? 1 : 0,
				MovementSettings ? MovementSettings->AirStrafeMultiplier : -1.0f));
	}
#endif

	if (!MovementSettings || MovementSettings->AirStrafeMultiplier <= 0.0f)
	{
		return;
	}

	// Acceleration, not GetLastInputVector: this runs inside the movement simulation now, and
	// Acceleration is what the server and a replay have. See IsAccelerationForward.
	const FVector InputVector = Acceleration;

#if WITH_EDITOR
	// Debug: show why air dive might not work
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9997, 0.0f, FColor::Yellow,
			FString::Printf(TEXT("AirStrafe: Input=(%.2f,%.2f,%.2f), Forward=%d, Controller=%d, DiveEnabled=%d"),
				InputVector.X, InputVector.Y, InputVector.Z,
				IsAccelerationForward() ? 1 : 0,
				OwnerController ? 1 : 0,
				MovementSettings->bEnableAirDive ? 1 : 0));
	}
#endif

	if (InputVector.IsNearlyZero())
	{
		return;
	}

	// Check for air dive: forward input + looking down + feature enabled
	// Fallback: try to get controller if not cached (can happen if possessed after BeginPlay)
	if (!OwnerController && OwnerCharacter)
	{
		OwnerController = Cast<APlayerController>(OwnerCharacter->GetController());
	}

	if (MovementSettings->bEnableAirDive && IsAccelerationForward() && OwnerController)
	{
		const float CameraPitch = OwnerController->GetControlRotation().Pitch;
		// Normalize pitch to -180 to 180 range (UE stores as 0-360)
		const float NormalizedPitch = FMath::UnwindDegrees(CameraPitch);

#if WITH_EDITOR
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(9999, 0.0f, FColor::Cyan,
				FString::Printf(TEXT("AirDive: RawPitch=%.1f, Normalized=%.1f, Threshold=%.1f, Active=%s"),
					CameraPitch, NormalizedPitch, MovementSettings->AirDiveAngleThreshold,
					NormalizedPitch < MovementSettings->AirDiveAngleThreshold ? TEXT("YES") : TEXT("NO")));
		}
#endif

		// If looking down past threshold, apply camera-directed acceleration
		if (NormalizedPitch < MovementSettings->AirDiveAngleThreshold)
		{
			// Get camera forward direction (includes pitch)
			const FRotator ControlRotation = OwnerController->GetControlRotation();
			FVector CameraForward = ControlRotation.Vector();

			// Calculate wish direction: XY from camera, Z scaled by multiplier
			FVector DiveDir = FVector(CameraForward.X, CameraForward.Y, CameraForward.Z * MovementSettings->AirDiveZMultiplier);
			DiveDir.Normalize();

			// Apply acceleration in dive direction
			const float DiveAccel = MovementSettings->AirAcceleration * MovementSettings->AirStrafeMultiplier * DeltaTime;
			Velocity += DiveDir * DiveAccel;

#if WITH_EDITOR
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(9998, 0.0f, FColor::Green,
					FString::Printf(TEXT("DIVING! Accel=%.1f, DiveDir=(%.2f, %.2f, %.2f)"),
						DiveAccel, DiveDir.X, DiveDir.Y, DiveDir.Z));
			}
#endif

			return; // Skip normal air strafe when diving
		}
	}

	// Normal air strafe (horizontal only)
	// Player can always accelerate up to AirSpeedCap in any direction,
	// and always brake (input opposite to velocity). Cannot exceed AirSpeedCap.
	const FVector WishDir = InputVector.GetSafeNormal2D();
	if (WishDir.IsNearlyZero())
	{
		return;
	}

	const FVector HorizontalVelocity = FVector(Velocity.X, Velocity.Y, 0.0f);
	const float CurrentSpeed = HorizontalVelocity.Size();
	const float MaxSpeed = MovementSettings->AirSpeedCap;
	const float Accel = MovementSettings->AirAcceleration * MovementSettings->AirStrafeMultiplier * DeltaTime;

	// Apply acceleration in wish direction
	FVector NewHorizontalVelocity = HorizontalVelocity + WishDir * Accel;
	const float NewSpeed = NewHorizontalVelocity.Size();

	// Only clamp if we INCREASED speed AND are above cap.
	// If we were already above cap (from slide/wallrun/dash), allow braking but not further acceleration.
	if (NewSpeed > MaxSpeed && NewSpeed > CurrentSpeed)
	{
		// Were we already above cap before this frame?
		if (CurrentSpeed > MaxSpeed)
		{
			// Already above cap - don't allow any speed increase, keep original speed
			NewHorizontalVelocity = NewHorizontalVelocity.GetSafeNormal() * CurrentSpeed;
		}
		else
		{
			// Below cap, would go above - clamp to cap
			NewHorizontalVelocity = NewHorizontalVelocity.GetSafeNormal() * MaxSpeed;
		}
	}

	Velocity.X = NewHorizontalVelocity.X;
	Velocity.Y = NewHorizontalVelocity.Y;
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

// ==================== Dash ====================

bool UApexMovementComponent::CanGroundDash() const
{
	if (!MovementSettings || !CharacterOwner || !IsMovingOnGround() || bIsGroundDashing
		|| bIsSliding || bIsMantling || bIsWallRunning || IsCrouching() || bRunLaunchActive)
	{
		return false;
	}

	return GroundDashCooldownRemaining <= 0.0f;
}

bool UApexMovementComponent::TryGroundDash()
{
	if (!CanGroundDash())
	{
		return false;
	}

	// Acceleration, not GetLastInputVector: the latter is local to whoever read the keyboard, so the
	// server dashed a client straight ahead regardless of which way they actually pushed.
	FVector DashDirection = Acceleration.GetSafeNormal2D();
	if (DashDirection.IsNearlyZero())
	{
		DashDirection = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
	}
	if (DashDirection.IsNearlyZero())
	{
		return false;
	}

	const float CurrentHorizontalSpeed = Velocity.Size2D();
	GroundDashDirection = DashDirection;
	GroundDashSpeed = FMath::Max(CurrentHorizontalSpeed, MovementSettings->GroundDashSpeed);
	GroundDashTimeRemaining = MovementSettings->GroundDashDuration;
	GroundDashCooldownRemaining = MovementSettings->GroundDashCooldown;
	bIsGroundDashing = true;

	Velocity.X = GroundDashDirection.X * GroundDashSpeed;
	Velocity.Y = GroundDashDirection.Y * GroundDashSpeed;

	OnGroundDashStarted.Broadcast();
	OnDashStateChanged.Broadcast();
	return true;
}

void UApexMovementComponent::UpdateGroundDash(float DeltaTime)
{
	if (!IsMovingOnGround() || GroundDashDirection.IsNearlyZero())
	{
		EndGroundDash();
		return;
	}

	GroundDashTimeRemaining -= DeltaTime;
	if (GroundDashTimeRemaining <= 0.0f)
	{
		EndGroundDash();
		return;
	}

	Velocity.X = GroundDashDirection.X * GroundDashSpeed;
	Velocity.Y = GroundDashDirection.Y * GroundDashSpeed;
}

void UApexMovementComponent::EndGroundDash()
{
	if (!bIsGroundDashing)
	{
		return;
	}

	bIsGroundDashing = false;
	GroundDashTimeRemaining = 0.0f;
	GroundDashDirection = FVector::ZeroVector;
	GroundDashSpeed = 0.0f;
	OnGroundDashEnded.Broadcast();
	OnDashStateChanged.Broadcast();
}

// ==================== Air Dash ====================

bool UApexMovementComponent::CanAirDash() const
{
	if (!MovementSettings || !IsFalling() || bIsAirDashing || bIsGroundDashing || bIsMantling || bIsWallRunning)
	{
		return false;
	}

	if (const APolarityCharacter* PolChar = Cast<APolarityCharacter>(GetOwner()))
	{
		if (!PolChar->bCanAirDash)
		{
			return false;
		}
	}

	if (AirDashCooldownRemaining > 0.0f)
	{
		return false;
	}

	return RemainingAirDashCount > 0;
}

bool UApexMovementComponent::TryAirDash()
{
	if (!CanAirDash() || !CharacterOwner || !MovementSettings)
	{
		return false;
	}

	RemainingAirDashCount--;

	// Calculate target dash direction
	FVector DashDirection;
	const FVector InputDir = Acceleration;
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

	// Get current horizontal speed
	const FVector HorizontalVelocity = FVector(Velocity.X, Velocity.Y, 0.0f);
	const float CurrentHorizontalSpeed = HorizontalVelocity.Size();

	// Check if we should redirect or do standard dash
	const bool bShouldRedirect = MovementSettings->bEnableAirDashRedirect
		&& CurrentHorizontalSpeed > MovementSettings->AirDashSpeed
		&& CurrentHorizontalSpeed >= MovementSettings->AirDashRedirectMinSpeed;

	if (bShouldRedirect)
	{
		// Velocity Redirect: keep speed, rotate direction over time
		bIsRedirecting = true;
		bIsAirDashing = true;
		AirDashRedirectTimeRemaining = MovementSettings->AirDashRedirectDuration;
		AirDashRedirectSpeed = CurrentHorizontalSpeed;
		AirDashRedirectStartDirection = HorizontalVelocity.GetSafeNormal();
		AirDashRedirectTargetDirection = DashDirection;

		// Immediately zero out vertical velocity for the redirect
		Velocity.Z = 0.0f;
	}
	else
	{
		// Standard dash: set velocity to AirDashSpeed
		bIsAirDashing = true;
		Velocity = DashDirection * MovementSettings->AirDashSpeed;
		Velocity.Z = 0.0f;

		// Start decay timer
		AirDashDecayTimeRemaining = MovementSettings->AirDashDecayDuration;
	}

	// Broadcast air dash started event
	OnAirDashStarted.Broadcast();
	OnDashStateChanged.Broadcast();
	return true;
}

void UApexMovementComponent::UpdateAirDash(float DeltaTime)
{
	bIsAirDashing = false;

	if (MovementSettings)
	{
		AirDashCooldownRemaining = MovementSettings->AirDashCooldown;
	}

	// Broadcast air dash ended event
	OnAirDashEnded.Broadcast();
	OnDashStateChanged.Broadcast();
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

void UApexMovementComponent::UpdateAirDashRedirect(float DeltaTime)
{
	if (!bIsRedirecting || !MovementSettings)
	{
		return;
	}

	AirDashRedirectTimeRemaining -= DeltaTime;

	if (AirDashRedirectTimeRemaining <= 0.0f)
	{
		// Redirect complete - snap to target direction
		Velocity = AirDashRedirectTargetDirection * AirDashRedirectSpeed;
		Velocity.Z = 0.0f;

		bIsRedirecting = false;
		bIsAirDashing = false;
		AirDashCooldownRemaining = MovementSettings->AirDashCooldown;

		// Start decay timer after redirect completes
		AirDashDecayTimeRemaining = MovementSettings->AirDashDecayDuration;
		OnDashStateChanged.Broadcast();
		return;
	}

	// Calculate interpolation alpha (0 = start, 1 = end)
	const float TotalDuration = MovementSettings->AirDashRedirectDuration;
	const float Alpha = 1.0f - (AirDashRedirectTimeRemaining / TotalDuration);

	// Smoothstep for nicer feel
	const float SmoothAlpha = FMath::SmoothStep(0.0f, 1.0f, Alpha);

	// Slerp between directions to maintain constant speed
	const FVector CurrentDirection = FMath::Lerp(AirDashRedirectStartDirection, AirDashRedirectTargetDirection, SmoothAlpha).GetSafeNormal();

	// Apply velocity with preserved speed
	Velocity = CurrentDirection * AirDashRedirectSpeed;
	Velocity.Z = 0.0f;  // Keep horizontal during redirect
}

void UApexMovementComponent::ResetAirAbilities()
{
	RemainingAirDashCount = MovementSettings ? MovementSettings->MaxAirDashCount : 1;
	bIsAirDashing = false;
	bIsRedirecting = false;
	AirDashRedirectTimeRemaining = 0.0f;
	OnDashStateChanged.Broadcast();
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
	else if (bIsGroundDashing)
		NewState = EPolarityMovementState::GroundDashing;
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

		// Stamp the moment sprinting ended. The weapon raise gate measures from here, so it has to
		// be the real transition out of the sprint pose and not the moment the trigger was pulled:
		// those differ whenever something else was already keeping the character out of a sprint.
		if (OldState == EPolarityMovementState::Sprinting)
		{
			if (const UWorld* World = GetWorld())
			{
				SprintEndTime = World->GetTimeSeconds();
			}
		}

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
				if (!VelocityDelta.IsNearlyZero())
				{
					// Convert velocity delta back to force: F = m * a = m * (dv / dt)
					// AddForce expects force, and will apply it as: a = F/m, dv = a*dt
					float CharMass = Mass > 0.0f ? Mass : 100.0f;
					FVector Force = VelocityDelta * CharMass / DeltaTime;

					// Use AddForce so CharacterMovement integrates it properly
					AddForce(Force);
				}
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

void UApexMovementComponent::ResetMovementState()
{
	// End any active movement states
	if (bIsSliding)
	{
		bIsSliding = false;
		SlideDuration = 0.0f;
		GroundFriction = (DefaultGroundFriction > 0.0f) ? DefaultGroundFriction : 8.0f;
		BrakingDecelerationWalking = (DefaultBrakingDeceleration > 0.0f) ? DefaultBrakingDeceleration : 2048.0f;
	}

	if (bIsWallRunning)
	{
		bIsWallRunning = false;
		WallRunSide = EWallSide::None;
		RestoreWallRunCapsule();
	}

	// Reset cooldowns
	SlideCooldownRemaining = 0.0f;
	SlideBoostCooldownRemaining = 0.0f;
	WallRunSameWallCooldown = 0.0f;
	AirDashCooldownRemaining = 0.0f;
	GroundDashCooldownRemaining = 0.0f;
	EndGroundDash();

	// Reset fatigue
	SlideFatigueCounter = 0;
	SlideFatigueDecayTimer = 0.0f;

	// Reset jump count
	CurrentJumpCount = 0;

	// Reset camera effects
	CurrentWallRunCameraRoll = 0.0f;
	CurrentWallRunCameraOffset = FVector::ZeroVector;
	CurrentWallRunMeshRoll = 0.0f;
	CurrentWallRunMeshPitch = 0.0f;
	CurrentWallRunCameraTilt = FRotator::ZeroRotator;

	// Reset input state
	bWantsToSprint = false;
	bSprintKeyHeld = false;
	SprintSuppressedUntil = -1000.0f;
	bWantsSlideOnLand = false;

	// Stop velocity
	Velocity = FVector::ZeroVector;
}
