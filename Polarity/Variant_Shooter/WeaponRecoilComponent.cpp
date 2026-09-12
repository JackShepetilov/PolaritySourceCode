// WeaponRecoilComponent.cpp
// Apex Legends-style recoil: fixed curve pattern, exact-magnitude spring delivery into a separate
// view layer, and springs that are themselves the whole recovery system

#include "WeaponRecoilComponent.h"
#include "ApexMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"

namespace
{
	constexpr float SpringEuler = 2.71828183f;

	// How far a spring x'' = -k*x - c*x', started at rest, travels from a velocity impulse of 1.
	//
	// Every kick is authored in degrees, and the player is entitled to get those degrees: the
	// spring's job is to decide HOW FAST the view gets there, never how far it goes. So the impulse
	// is divided by this factor, and retuning a spring changes the timing of a shot without
	// changing its strength. The three cases are the three regimes of the damped oscillator, and
	// they have to be spelled out because the recoil springs deliberately run in all three (the hot
	// sets sit at k = 0, where there is no oscillator left at all).
	float SpringPeakPerUnitImpulse(float Stiffness, float Damping)
	{
		const float C = FMath::Max(Damping, KINDA_SMALL_NUMBER);

		// k = 0: nothing pulls back, the impulse just bleeds off. x(t) -> v0 / c.
		if (Stiffness <= KINDA_SMALL_NUMBER)
		{
			return 1.0f / C;
		}

		const float Omega = FMath::Sqrt(Stiffness);
		const float Zeta = C / (2.0f * Omega);

		if (Zeta < 1.0f - KINDA_SMALL_NUMBER)
		{
			// Underdamped: overshoots zero and rings back. Peak at the first quarter swing.
			const float OmegaD = Omega * FMath::Sqrt(1.0f - Zeta * Zeta);
			const float TPeak = FMath::Atan2(OmegaD, Zeta * Omega) / OmegaD;
			return (1.0f / OmegaD) * FMath::Exp(-Zeta * Omega * TPeak) * FMath::Sin(OmegaD * TPeak);
		}

		if (Zeta > 1.0f + KINDA_SMALL_NUMBER)
		{
			// Overdamped: crawls out and crawls back, never crossing zero.
			const float OmegaS = Omega * FMath::Sqrt(Zeta * Zeta - 1.0f);
			const float TPeak = FMath::Loge((Zeta * Omega + OmegaS) / (Zeta * Omega - OmegaS)) / (2.0f * OmegaS);
			return (1.0f / (2.0f * OmegaS))
				* (FMath::Exp((-Zeta * Omega + OmegaS) * TPeak) - FMath::Exp((-Zeta * Omega - OmegaS) * TPeak));
		}

		// Critically damped: x(t) = v0 * t * e^(-omega t), peaking at t = 1/omega.
		return 1.0f / (SpringEuler * Omega);
	}

	float ImpulseVelocityFor(float Degrees, float Stiffness, float Damping)
	{
		const float Peak = SpringPeakPerUnitImpulse(Stiffness, Damping);
		return Peak > KINDA_SMALL_NUMBER ? Degrees / Peak : 0.0f;
	}
}

UWeaponRecoilComponent::UWeaponRecoilComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UWeaponRecoilComponent::BeginPlay()
{
	Super::BeginPlay();

	// Deterministic streams seeded per component instance: same burst -> same recoil, so tuning
	// changes can be A/B compared shot for shot.
	const int32 Seed = static_cast<int32>(GetUniqueID());
	RecoilRandomStream.Initialize(Seed);

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

	// The pattern's X axis is the shot counter, so nothing to advance per frame here — only the
	// decision that the silence has lasted long enough to count as a new burst.
	// GetBurstResetThreshold() derives that from the weapon's own refire rate.
	if (!bIsFiring && ShotsInBurst > 0 && TimeSinceLastShot > GetBurstResetThreshold())
	{
		ShotsInBurst = 0;
	}

	// Measure look speed from raw mouse input and update tracking suppression BEFORE sway
	// consumes (and zeroes) the per-frame deltas. Raw input is used so recoil itself cannot
	// feed back into the measurement.
	{
		const float SafeDt = FMath::Max(DeltaTime, 1e-4f);
		const float RawLookSpeed = (FMath::Abs(CurrentMouseVelocity.X) + FMath::Abs(CurrentMouseVelocity.Y)) / SafeDt;
		SmoothedLookSpeed = FMath::FInterpTo(SmoothedLookSpeed, RawLookSpeed, DeltaTime, 8.0f);
	}
	UpdateSmoothingMultiplier(DeltaTime);

	// Heat first: it picks which spring set the punch springs run on this frame.
	UpdateSpringHeat(DeltaTime);
	UpdatePunchSprings(DeltaTime);
	UpdateVisualKick(DeltaTime);
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

float UWeaponRecoilComponent::GetBurstResetThreshold() const
{
	// Roughly two missed shots means the player let go; never shorter than the legacy 0.3 s so
	// behaviour stays sane when the caller does not know its interval.
	return FMath::Max(LastShotInterval * 2.0f, 0.3f);
}

void UWeaponRecoilComponent::OnWeaponFired(float ShotIntervalSeconds)
{
	// A pause longer than the burst threshold starts the pattern over even if OnFiringEnded was
	// not seen (e.g. semi-auto tapping).
	if (!bIsFiring || TimeSinceLastShot > GetBurstResetThreshold())
	{
		ShotsInBurst = 0;
	}

	bIsFiring = true;
	TimeSinceLastShot = 0.0f;
	LastShotInterval = FMath::Max(ShotIntervalSeconds, 0.0f);

	// Heat the spring BEFORE the shot is queued, so this shot is already delivered under the set
	// it belongs to. Queueing first would hand the opening round of every burst to the cold spring
	// and quietly bounce it back out from under the pattern.
	SpringHeat = FMath::Clamp(SpringHeat + Settings.Recovery.HeatPerShot, 0.0f, 1.0f);

	// Calculate total recoil for this shot (reads ShotsInBurst as the pattern's X)
	FRotator TotalRecoil = CalculateShotRecoil();
	++ShotsInBurst;

	// Split recoil between camera and viewmodel based on ADS state
	const float Fraction = bIsAiming ? Settings.VisualKick.ADSWeaponFraction : Settings.VisualKick.HipfireWeaponFraction;
	const float VMScale = bIsAiming ? Settings.VisualKick.ADSVMScale : Settings.VisualKick.HipfireVMScale;

	FRotator CameraRecoil = TotalRecoil * (1.0f - Fraction);
	FRotator ViewmodelRecoil = TotalRecoil * Fraction * VMScale;

	// Camera portion goes into the punch layer, never into the controller.
	QueuePunch(CameraRecoil);

	// Independent roll: cosmetic twist around the barrel axis, random direction per shot,
	// deliberately NOT part of the camera/viewmodel split.
	const auto& VK = Settings.VisualKick;
	float RollMagnitude = RecoilRandomStream.FRandRange(VK.RollRandomMin, VK.RollRandomMax);
	float RollSign = RecoilRandomStream.FRand() < 0.5f ? -1.0f : 1.0f;
	float ShotRoll = RollSign * RollMagnitude * VK.RollHardScale;

	// Trigger visual effects
	if (VK.bEnableVisualKick)
	{
		TriggerVisualKick(ViewmodelRecoil, ShotRoll);
	}

	UE_LOG(LogTemp, Verbose, TEXT("Recoil: Shot=%d, Smooth=%.2f, Total=(P:%.2f, Y:%.2f), Camera=(P:%.2f, Y:%.2f), VM=(P:%.2f, Y:%.2f)"),
		ShotsInBurst - 1, CurrentSmoothingMultiplier,
		TotalRecoil.Pitch, TotalRecoil.Yaw,
		CameraRecoil.Pitch, CameraRecoil.Yaw,
		ViewmodelRecoil.Pitch, ViewmodelRecoil.Yaw);
}

void UWeaponRecoilComponent::FireWithOverrideSettings(const FWeaponRecoilSettings& OverrideSettings)
{
	// Save current state
	FWeaponRecoilSettings SavedSettings = Settings;
	const int32 SavedShotsInBurst = ShotsInBurst;
	const float SavedShotInterval = LastShotInterval;

	// Apply override — a one-off shot plays its pattern from the first key
	Settings = OverrideSettings;
	ShotsInBurst = 0;

	// Fire with override settings
	OnWeaponFired();

	// Restore original state
	Settings = SavedSettings;
	ShotsInBurst = SavedShotsInBurst;
	LastShotInterval = SavedShotInterval;
}

void UWeaponRecoilComponent::OnFiringEnded()
{
	bIsFiring = false;
}

void UWeaponRecoilComponent::ResetRecoil()
{
	ShotsInBurst = 0;
	LastShotInterval = 0.0f;
	SpringHeat = 0.0f;
	bIsFiring = false;

	PunchOffset = FRotator::ZeroRotator;

	CurrentSmoothingMultiplier = 1.0f;

	CurrentWeaponOffset = FVector::ZeroVector;
	CurrentWeaponRotation = FRotator::ZeroRotator;

	KickSpringPitch.Reset();
	KickSpringYaw.Reset();
	KickSpringRoll.Reset();
	KickSpringBack.Reset();

	CameraRecoilSpringPitch.Reset();
	CameraRecoilSpringYaw.Reset();
	CameraRecoilSpringRoll.Reset();

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

	// Nothing else happens here, and that is the point. Recoil lives in its own layer now, so the
	// mouse cannot be confused with it and there is no pending return for a pull-down to cancel:
	// the player's aim goes where they put it, the punch unwinds underneath it on its own.
}

// ==================== Internal Methods ====================

FRotator UWeaponRecoilComponent::CalculateShotRecoil()
{
	const auto& Pattern = Settings.Pattern;
	FRotator Recoil = FRotator::ZeroRotator;

	// The pattern shapes BOTH stances by default, which is what Apex does — hip-fire differs by
	// having a softer spring, a bigger share going to the weapon model and a wide spread cone on
	// top, not by losing the pattern. bPatternOnlyWhenAiming is left as an opt-out per weapon.
	const bool bUsePattern = Pattern.RecoilCurve != nullptr &&
		(!Pattern.bPatternOnlyWhenAiming || bIsAiming);

	float ScatterRadius = 0.0f;

	if (bUsePattern)
	{
		// Sample by SHOT NUMBER, not by elapsed time: the pattern is a table of "where does shot
		// N pull", exactly one key per round in the magazine, so the shape a player learns does
		// not change when the weapon's rate of fire is tuned or an upgrade speeds it up. Firing
		// past the last key rides the tail (the curve's clamped end), which keeps a reloaded
		// sustained burst predictable instead of looping back into the aggressive opening climb.
		const FVector Sample = Pattern.RecoilCurve->GetVectorValue(static_cast<float>(ShotsInBurst));
		Recoil.Pitch = Sample.X * Pattern.PatternScale;
		Recoil.Yaw = Sample.Y * Pattern.PatternScale;

		// Third channel: this shot's scatter radius in degrees. Curves authored before the channel
		// existed evaluate it as zero (an empty FRichCurve returns its default), so they keep
		// behaving exactly as they did.
		ScatterRadius = FMath::Max(Sample.Z, 0.0f) * Pattern.PatternScale * Pattern.PatternRandomScale;
	}
	else
	{
		Recoil.Pitch = Pattern.BaseVerticalRecoil;
		Recoil.Yaw = RecoilRandomStream.FRandRange(-Pattern.BaseHorizontalRecoil, Pattern.BaseHorizontalRecoil);
	}

	// Small symmetric multiplicative variance: same shape every burst, but never pixel-identical.
	// One shared factor per shot preserves the pitch/yaw ratio of the authored pattern.
	const float Variance = 1.0f + RecoilRandomStream.FRandRange(-Pattern.PatternVariance, Pattern.PatternVariance);
	Recoil.Pitch *= Variance;
	Recoil.Yaw *= Variance;

	// Scatter, and it has to be TWO independent draws. One shared draw (which is what Variance is)
	// only stretches the kick along the direction the curve already chose, so a magazine emptied
	// into a wall traces the same line every time, just longer or shorter. Two draws move the shot
	// off that line, which is the only thing that turns a trace into a group.
	if (ScatterRadius > 0.0f)
	{
		Recoil.Pitch += RecoilRandomStream.FRandRange(-ScatterRadius, ScatterRadius);
		Recoil.Yaw += RecoilRandomStream.FRandRange(-ScatterRadius, ScatterRadius);
	}

	// Camera roll, a full third axis of the kick in Apex. Sign is random per shot; the very stiff
	// roll spring turns it into a short ring rather than a lean.
	const float RollMag = Pattern.RollBase + RecoilRandomStream.FRandRange(-Pattern.RollRandom, Pattern.RollRandom);
	Recoil.Roll = RollMag * (RecoilRandomStream.FRand() < 0.5f ? -1.0f : 1.0f);

	// Posture multipliers (airborne, crouch, ADS, moving) and external scalar (upgrades)
	const float SituationalMult = GetSituationalMultiplier();
	Recoil.Pitch *= SituationalMult;
	Recoil.Yaw *= SituationalMult;
	Recoil.Roll *= SituationalMult;

	// Recoil smoothing: tracking a target suppresses the kick. Pitch and yaw only — those are aim
	// disturbance, and suppressing them is the whole point. Roll is feedback and disturbs nothing,
	// so taking it away while tracking would only make the weapon read as dead in the fight it is
	// most alive in.
	Recoil.Pitch *= CurrentSmoothingMultiplier;
	Recoil.Yaw *= CurrentSmoothingMultiplier;

	return Recoil;
}

float UWeaponRecoilComponent::GetSituationalMultiplier() const
{
	const auto& Sit = Settings.Situational;
	float Multiplier = 1.0f;

	// Airborne increases recoil
	if (IsAirborne())
	{
		Multiplier *= Sit.AirborneMultiplier;
	}

	// Crouching reduces recoil
	if (bIsCrouching)
	{
		Multiplier *= Sit.CrouchMultiplier;
	}

	// ADS reduces recoil
	if (bIsAiming)
	{
		Multiplier *= Sit.ADSMultiplier;
	}

	// Moving increases recoil slightly
	if (IsMoving() && !IsAirborne())
	{
		Multiplier *= Sit.MovingMultiplier;
	}

	// External scalar (e.g. ADS time-dilation upgrade reduces recoil while active)
	Multiplier *= ExternalRecoilMultiplier;

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

void UWeaponRecoilComponent::QueuePunch(const FRotator& Recoil)
{
	const FRecoilSpringSet Spring = ResolveActiveSpringSet();
	const float Hard = FMath::Clamp(Settings.Recovery.HardFraction, 0.0f, 1.0f);
	const float Soft = 1.0f - Hard;

	// Every shot arrives in two pieces, the same two Apex authors as hardScale and softScale.
	//
	// The hard piece lands on the offset this instant, because a snap is a thing a spring cannot
	// produce: a spring can only approach a value, and the first frames of an approach are the
	// slowest ones. The soft piece goes in as a velocity impulse and is sized so the spring's own
	// peak comes out at exactly the authored degrees, which is what keeps the two knobs honest:
	// retuning a spring changes WHEN a kick arrives and never HOW BIG it is.
	CameraRecoilSpringPitch.Value += Recoil.Pitch * Hard;
	CameraRecoilSpringYaw.Value   += Recoil.Yaw   * Hard;
	CameraRecoilSpringRoll.Value  += Recoil.Roll  * Hard;

	CameraRecoilSpringPitch.Velocity += ImpulseVelocityFor(Recoil.Pitch * Soft, Spring.PitchConstant, Spring.PitchDamping);
	CameraRecoilSpringYaw.Velocity   += ImpulseVelocityFor(Recoil.Yaw   * Soft, Spring.YawConstant,   Spring.YawDamping);
	CameraRecoilSpringRoll.Velocity  += ImpulseVelocityFor(Recoil.Roll  * Soft, Spring.RollConstant,  Spring.RollDamping);
}

// ==================== Punch Springs ====================

float UWeaponRecoilComponent::ResolveHeatHoldTime() const
{
	const float Authored = Settings.Recovery.HeatHoldTime;
	if (Authored > KINDA_SMALL_NUMBER)
	{
		return Authored;
	}

	// Default: the weapon's own refire interval. A held trigger then never opens a gap long enough
	// to cool and a tap always does, without either behaviour having to be written down per weapon.
	// Apex hand-authors the same thing (0.08 on the R-301 against a ~0.074 s interval).
	return FMath::Max(LastShotInterval, 0.05f);
}

FRecoilSpringSet UWeaponRecoilComponent::ResolveActiveSpringSet() const
{
	const FRecoilRecoverySettings& R = Settings.Recovery;
	const FRecoilSpringSet& Cold = bIsAiming ? R.ADS : R.Hipfire;
	const FRecoilSpringSet& Hot  = bIsAiming ? R.ADSHot : R.HipfireHot;

	if (SpringHeat <= KINDA_SMALL_NUMBER)
	{
		return Cold;
	}
	if (SpringHeat >= 1.0f - KINDA_SMALL_NUMBER)
	{
		return Hot;
	}

	FRecoilSpringSet Blend;
	Blend.PitchConstant = FMath::Lerp(Cold.PitchConstant, Hot.PitchConstant, SpringHeat);
	Blend.PitchDamping  = FMath::Lerp(Cold.PitchDamping,  Hot.PitchDamping,  SpringHeat);
	Blend.YawConstant   = FMath::Lerp(Cold.YawConstant,   Hot.YawConstant,   SpringHeat);
	Blend.YawDamping    = FMath::Lerp(Cold.YawDamping,    Hot.YawDamping,    SpringHeat);
	Blend.RollConstant  = FMath::Lerp(Cold.RollConstant,  Hot.RollConstant,  SpringHeat);
	Blend.RollDamping   = FMath::Lerp(Cold.RollDamping,   Hot.RollDamping,   SpringHeat);
	return Blend;
}

void UWeaponRecoilComponent::UpdateSpringHeat(float DeltaTime)
{
	if (SpringHeat <= 0.0f)
	{
		return;
	}

	// Hold, then fade. The hold is the whole mechanism: heat is set to full by a single shot, so
	// what separates a tap from a burst is not how much heat there is but how long it lasts.
	if (TimeSinceLastShot < ResolveHeatHoldTime())
	{
		return;
	}

	const float Fade = FMath::Max(Settings.Recovery.HeatFadeTime, 0.01f);
	SpringHeat = FMath::Max(0.0f, SpringHeat - DeltaTime / Fade);
}

void UWeaponRecoilComponent::UpdatePunchSprings(float DeltaTime)
{
	const FRecoilSpringSet Spring = ResolveActiveSpringSet();

	// Target is zero, always. There is no recovery pass anywhere in this component and no recovery
	// speed to tune: these three springs pulling back to zero ARE the recovery, exactly as in Apex,
	// whose shipped ConVar help for the same constant reads "Bigger number increases the speed at
	// which the view corrects". When the hot set puts a constant at 0 the pull simply stops, the
	// burst piles up, and cooling hands the whole pile back in one motion.
	//
	// Sub-stepped because these springs are stiff — roll runs at 20000, about 22 Hz — and the
	// symplectic Euler step above is only stable while sqrt(k) * dt stays under 2.
	constexpr float MaxSubStep = 1.0f / 240.0f;
	float Remaining = DeltaTime;
	while (Remaining > KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(Remaining, MaxSubStep);
		CameraRecoilSpringPitch.UpdateWithDamping(0.0f, Spring.PitchConstant, Spring.PitchDamping, Step);
		CameraRecoilSpringYaw.UpdateWithDamping(0.0f, Spring.YawConstant, Spring.YawDamping, Step);
		CameraRecoilSpringRoll.UpdateWithDamping(0.0f, Spring.RollConstant, Spring.RollDamping, Step);
		Remaining -= Step;
	}

	PunchOffset.Pitch = CameraRecoilSpringPitch.Value;
	PunchOffset.Yaw   = CameraRecoilSpringYaw.Value;
	PunchOffset.Roll  = CameraRecoilSpringRoll.Value;

	// Nothing is pushed anywhere. AShooterCharacter::GetViewRotation pulls this value when the
	// camera is posed, which happens after every actor has ticked, so the reader always gets the
	// current frame and the component never has to know who is looking.
}

// ==================== Visual Kick (Spring-Damper) ====================

void UWeaponRecoilComponent::TriggerVisualKick(const FRotator& ViewmodelRecoil, float RollKick)
{
	// Apply as velocity impulses to springs (not target positions).
	// This preserves momentum from previous kicks, creating smooth continuous motion.
	// These four run on FBobSpringState::Update, which derives critical damping, so the peak
	// normalization has to be told the same damping the springs will actually use.
	const float Stiffness = Settings.VisualKick.KickSpringStiffness;
	const float Damping = 2.0f * FMath::Sqrt(Stiffness);
	KickSpringPitch.Velocity += ImpulseVelocityFor(ViewmodelRecoil.Pitch, Stiffness, Damping);
	KickSpringYaw.Velocity += ImpulseVelocityFor(ViewmodelRecoil.Yaw, Stiffness, Damping);
	KickSpringRoll.Velocity += ImpulseVelocityFor(RollKick, Stiffness, Damping);

	// KickBackDistance already carries its own sign convention (negative = backwards along barrel)
	KickSpringBack.Velocity += -ImpulseVelocityFor(Settings.VisualKick.KickBackDistance, Stiffness, Damping);
}

void UWeaponRecoilComponent::UpdateVisualKick(float DeltaTime)
{
	if (!Settings.VisualKick.bEnableVisualKick)
	{
		// Kick is the only writer of these two, so an early return that skips the write leaves
		// them frozen at whatever the last enabled frame produced, parking the weapon off centre
		// for as long as the flag stays off. Zero them on the way out.
		CurrentWeaponRotation = FRotator::ZeroRotator;
		CurrentWeaponOffset = FVector::ZeroVector;
		return;
	}

	// Sub-step visual kick springs to prevent instability on large DeltaTime
	constexpr float MaxSubStep = 1.0f / 60.0f;
	const float Stiffness = Settings.VisualKick.KickSpringStiffness;
	float Remaining = DeltaTime;
	while (Remaining > KINDA_SMALL_NUMBER)
	{
		float Step = FMath::Min(Remaining, MaxSubStep);
		KickSpringPitch.Update(0.0f, Stiffness, Step);
		KickSpringYaw.Update(0.0f, Stiffness, Step);
		KickSpringRoll.Update(0.0f, Stiffness, Step);
		KickSpringBack.Update(0.0f, Stiffness, Step);
		Remaining -= Step;
	}

	// Read spring values into current weapon transform
	CurrentWeaponRotation.Pitch = KickSpringPitch.Value;
	CurrentWeaponRotation.Yaw = KickSpringYaw.Value;
	CurrentWeaponRotation.Roll = KickSpringRoll.Value;
	CurrentWeaponOffset.X = KickSpringBack.Value;
}

// ==================== Weapon Sway ====================

void UWeaponRecoilComponent::UpdateWeaponSway(float DeltaTime)
{
	if (!Settings.Sway.bEnableWeaponSway)
	{
		// Same reasoning as UpdateVisualKick: sway owns this value, so it has to clear it rather
		// than leave the last frame's offset standing.
		CurrentSwayOffset = FRotator::ZeroRotator;
		return;
	}

	const auto& Sway = Settings.Sway;

	// Smooth mouse velocity
	SmoothedMouseVelocity = FMath::Vector2DInterpTo(
		SmoothedMouseVelocity,
		CurrentMouseVelocity,
		DeltaTime,
		Sway.MouseSwayLag
	);

	// Reset current mouse velocity (it gets set each frame from input)
	CurrentMouseVelocity = FVector2D::ZeroVector;

	// Calculate mouse sway
	FRotator MouseSway = FRotator::ZeroRotator;
	MouseSway.Yaw = FMath::Clamp(
		-SmoothedMouseVelocity.X * Sway.MouseSwayIntensity,
		-Sway.MaxMouseSwayOffset,
		Sway.MaxMouseSwayOffset
	);
	MouseSway.Pitch = FMath::Clamp(
		SmoothedMouseVelocity.Y * Sway.MouseSwayIntensity,
		-Sway.MaxMouseSwayOffset,
		Sway.MaxMouseSwayOffset
	);

	// Ornstein-Uhlenbeck organic sway (truly stochastic, never repeats)
	FRotator OrganicSway = FRotator::ZeroRotator;

	if (Sway.bEnableOrganicSway)
	{
		// Layer 1: Breathing (slow, large drift)
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			BreathingOU[Axis] = AdvanceOU(BreathingOU[Axis],
				Sway.BreathingReversionSpeed, Sway.BreathingVolatility,
				Sway.BreathingMaxAngle, DeltaTime);
		}
		OrganicSway.Pitch += BreathingOU[0] * Sway.BreathingAxisScale.X;
		OrganicSway.Yaw   += BreathingOU[1] * Sway.BreathingAxisScale.Y;
		OrganicSway.Roll  += BreathingOU[2] * Sway.BreathingAxisScale.Z;

		// Layer 2: Tremor (medium speed, hand instability)
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			TremorOU[Axis] = AdvanceOU(TremorOU[Axis],
				Sway.TremorReversionSpeed, Sway.TremorVolatility,
				Sway.TremorMaxAngle, DeltaTime);
		}
		OrganicSway.Pitch += TremorOU[0] * Sway.TremorAxisScale.X;
		OrganicSway.Yaw   += TremorOU[1] * Sway.TremorAxisScale.Y;
		OrganicSway.Roll  += TremorOU[2] * Sway.TremorAxisScale.Z;

		// Layer 3: Micro-jitter (fast, nervous system noise)
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			JitterOU[Axis] = AdvanceOU(JitterOU[Axis],
				Sway.JitterReversionSpeed, Sway.JitterVolatility,
				Sway.JitterMaxAngle, DeltaTime);
		}
		OrganicSway.Pitch += JitterOU[0] * Sway.JitterAxisScale.X;
		OrganicSway.Yaw   += JitterOU[1] * Sway.JitterAxisScale.Y;
		OrganicSway.Roll  += JitterOU[2] * Sway.JitterAxisScale.Z;
	}

	// Movement sway multiplier
	float MovementMult = 1.0f;
	if (IsMoving())
	{
		MovementMult = Sway.MovementSwayMultiplier;
	}

	// Reduce sway when aiming
	float AimMult = bIsAiming ? Sway.ADSSwayMultiplier : 1.0f;

	// Combine all sway sources
	FRotator TotalSway = (MouseSway + OrganicSway * MovementMult) * AimMult * SwayOverrideMultiplier;

	// Smooth interpolation to target sway
	CurrentSwayOffset = FMath::RInterpTo(CurrentSwayOffset, TotalSway, DeltaTime, Sway.MouseSwayLag);

	// Sway is NOT folded into CurrentWeaponRotation. It used to be, and that made the two
	// inseparable at the getter: kick assigned the variable, sway added to it, and one call
	// returned the sum. ADS needs them apart (see GetWeaponKickRotation / GetWeaponSwayRotation).
}

// ==================== Ornstein-Uhlenbeck Process ====================

float UWeaponRecoilComponent::AdvanceOU(float CurrentValue, float ReversionSpeed, float Volatility, float MaxAngle, float DeltaTime)
{
	// Ornstein-Uhlenbeck: dX = θ(μ - X)dt + σ * dW
	// μ = 0 (center), θ = ReversionSpeed, σ = Volatility
	// Approximate Gaussian noise via Central Limit Theorem (sum of 3 uniforms)
	float U1 = RecoilRandomStream.FRandRange(-1.0f, 1.0f);
	float U2 = RecoilRandomStream.FRandRange(-1.0f, 1.0f);
	float U3 = RecoilRandomStream.FRandRange(-1.0f, 1.0f);
	float GaussianApprox = (U1 + U2 + U3) / 1.732f; // ~N(0,1)

	float Drift = ReversionSpeed * (0.0f - CurrentValue) * DeltaTime;
	float Diffusion = Volatility * GaussianApprox * FMath::Sqrt(DeltaTime);

	float NewValue = CurrentValue + Drift + Diffusion;
	return FMath::Clamp(NewValue, -MaxAngle, MaxAngle);
}

// ==================== Editor Seeding ====================

void UWeaponRecoilComponent::SeedRecoilCurve(UCurveVector* Curve, const TArray<FVector>& Keys, const TArray<float>& ScatterRadii)
{
#if WITH_EDITOR
	if (!Curve)
	{
		return;
	}

	Curve->Modify();
	for (FRichCurve& Channel : Curve->FloatCurves)
	{
		Channel.Reset();
	}

	FRichCurve& PitchCurve = Curve->FloatCurves[0];
	FRichCurve& YawCurve = Curve->FloatCurves[1];
	FRichCurve& ScatterCurve = Curve->FloatCurves[2];
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		const FVector& Key = Keys[Index];
		PitchCurve.AddKey(Key.X, Key.Y);
		YawCurve.AddKey(Key.X, Key.Z);

		// Short or empty ScatterRadii is not an error: it is how a caller says "no scatter", and
		// how every tuning script written before the third channel existed keeps working.
		if (ScatterRadii.IsValidIndex(Index))
		{
			ScatterCurve.AddKey(Key.X, FMath::Max(ScatterRadii[Index], 0.0f));
		}
	}
#endif
}

// ==================== Recoil Smoothing ====================

void UWeaponRecoilComponent::UpdateSmoothingMultiplier(float DeltaTime)
{
	const auto& Sm = Settings.Smoothing;

	float Target = 1.0f;
	if (Sm.bEnableRecoilSmoothing && Sm.MaxViewSpeed > Sm.MinViewSpeed)
	{
		// Smoothstep between MinViewSpeed (no reduction) and MaxViewSpeed (full reduction)
		const float Alpha = FMath::Clamp(
			(SmoothedLookSpeed - Sm.MinViewSpeed) / (Sm.MaxViewSpeed - Sm.MinViewSpeed),
			0.0f, 1.0f);
		const float Eased = Alpha * Alpha * (3.0f - 2.0f * Alpha);
		Target = FMath::Lerp(1.0f, Sm.MinMultiplier, Eased);
	}

	CurrentSmoothingMultiplier = FMath::FInterpTo(CurrentSmoothingMultiplier, Target, DeltaTime, Sm.InterpSpeed);
}
