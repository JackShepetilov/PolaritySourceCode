// WeaponRecoilComponent.h
// Apex Legends-style recoil: a fixed learnable pattern authored as a CurveVector, delivered to a
// SEPARATE view layer through per-stance springs that are also the only recovery there is, plus
// cosmetic weapon kick and organic sway

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CameraShakeComponent.h"
#include "Curves/CurveVector.h"
#include "WeaponRecoilComponent.generated.h"

class AShooterWeapon;
class UCameraShakeComponent;
class UCharacterMovementComponent;
class UApexMovementComponent;

/**
 * Pattern shape: where the aim point travels over a burst.
 *
 * The pattern lives in a CurveVector asset (one curve per weapon class, editable in the Curve
 * Editor). X-axis = shot number since the burst started (0 = first round), X channel = pitch
 * degrees (positive = up), Y channel = yaw degrees (positive = right), Z channel = the RADIUS of
 * this shot's random scatter in degrees. One key per round in the reference magazine, so a full
 * mag IS the whole curve, and the shape the player learns does not move when the weapon's rate of
 * fire is tuned. Firing past the last key rides the curve's tail.
 *
 * The Z channel is what keeps a magazine emptied into a wall from drawing the same line twice.
 * Apex authors it per bullet and moves it across the magazine (R-301: 0.40 on the opening rounds,
 * 0.25 through the middle, 0.45 in the tail), so the middle of the mag is deliberately the most
 * predictable part. PatternVariance below cannot do this job: it is one factor shared by both
 * axes, so it changes how FAR a shot kicks but never which way, and the trace on the wall comes
 * out identical every time.
 *
 * Apex rules implemented here: the pattern plays in BOTH stances (their springs.txt carries a
 * separate hipfire_* constant set per weapon, which is what proves hip-fire kicks too — the
 * stances differ by spring, by how much goes to the weapon model, and by spread, not by whether
 * there is a pattern), every shot of the same weapon has the same magnitude (no ramp, all of
 * Apex's viewkick_scale_* are 1.0 on modern weapons), and the scatter above keeps the shape
 * recognizable without being robotic.
 */
USTRUCT(BlueprintType)
struct FRecoilPatternSettings
{
	GENERATED_BODY()

	/** Aim pattern. Keys on SHOT NUMBER (0 = first round of the burst, one key per magazine
	 *  round); X = pitch (deg, up+), Y = yaw (deg, right+). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	TObjectPtr<UCurveVector> RecoilCurve;

	/** Overall strength of the authored pattern. Multiplies both pitch and yaw of the curve sample,
	 *  so the SHAPE the player learns is untouched and only how hard it pulls changes. This is the
	 *  knob to reach for when a generated curve came out too strong or too weak: retune here, not
	 *  by redrawing the curve, and one number keeps every weapon comparable. Does not touch the
	 *  fallback values below, which are already plain degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float PatternScale = 1.0f;

	/** Play the curve only while aiming. Off (the default, and what Apex does) = the pattern shapes
	 *  hip-fire too, with its own softer spring and a bigger share going to the weapon model. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	bool bPatternOnlyWhenAiming = false;

	/** Symmetric multiplicative variance per shot (0.1 = +/-10%). Scales the kick without turning
	 *  it: the direction of every shot stays exactly as authored. Use the curve's Z channel for
	 *  scatter that actually moves the impact around. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PatternVariance = 0.1f;

	/** Multiplies the per-shot scatter radius read from the curve's Z channel. One knob to make a
	 *  weapon tighter or looser without redrawing the curve; 0 disables scatter entirely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float PatternRandomScale = 1.0f;

	/** Camera ROLL per shot, degrees. Apex treats roll as a full third axis of the view kick
	 *  (R-301: base 0.8), delivered through a very stiff spring so it reads as a short ring rather
	 *  than a lean. Sign is random per shot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float RollBase = 0.8f;

	/** Symmetric random added to RollBase, degrees (Apex R-301 uses +/-0.2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float RollRandom = 0.2f;

	/** Vertical kick per shot when no curve is set (or hip-firing with bPatternOnlyWhenAiming). Deg, up+. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern|Fallback", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float BaseVerticalRecoil = 0.3f;

	/** Horizontal randomness per shot outside the pattern. Deg, symmetric around zero. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern|Fallback", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float BaseHorizontalRecoil = 0.15f;
};

/**
 * One spring, three axes. Mirrors a block of Apex's weapons/springs.txt one field for one field,
 * so their numbers can be copied across without conversion: both sides integrate the same
 * equation, x'' = -k*x - c*x'.
 *
 * Damping is stored as the raw coefficient c, NOT as a ratio, for the same reason Apex stores it
 * that way: the hot sets below run at k = 0, where a ratio would have nothing to scale.
 * For reading the numbers: the ratio is c / (2*sqrt(k)). Below 1 the spring overshoots zero and
 * swings back, at 1 it arrives without overshoot, above 1 it crawls in without ever reaching.
 */
USTRUCT(BlueprintType)
struct FRecoilSpringSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (ClampMin = "0.0"))
	float PitchConstant = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (ClampMin = "0.0"))
	float PitchDamping = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (ClampMin = "0.0"))
	float YawConstant = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (ClampMin = "0.0"))
	float YawDamping = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (ClampMin = "0.0"))
	float RollConstant = 20000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (ClampMin = "0.0"))
	float RollDamping = 22.0f;
};

/**
 * How the kick is delivered to the view and how the view gets back.
 *
 * There is no recovery speed and no recovery delay here, and their absence is the design. Apex
 * has neither: the ONLY thing that returns the view is the spring, and the developers' own
 * description of the spring constant in the shipped ConVar list says so outright ("Bigger number
 * increases the speed at which the view corrects"). A separate recovery pass walking a remembered
 * displacement back at a fixed number of degrees per second is what makes recoil feel like the
 * game moving the player's mouse.
 *
 * Permanence is not a number either. It falls out of the spring: while the trigger is held the
 * spring is HOT (constant 0, no restoring force at all), so every shot's kick simply stays and the
 * player has to fight the whole authored pattern. One shot's worth of quiet and it cools back to
 * the stiff set, which takes the accumulated offset away in one motion. Apex's R-301 and LMG both
 * ship hot sets with springConstant 0.0; the Wingman, a semi-auto, has no hot set at all. To get
 * that behaviour here, give a weapon the same values in both the cold and hot sets.
 */
USTRUCT(BlueprintType)
struct FRecoilRecoverySettings
{
	GENERATED_BODY()

	FRecoilRecoverySettings()
	{
		// Straight off Apex's rspn101_vkp / rspn101_vkp_hot. FRecoilSpringSet's own defaults are
		// already the cold hip set, so only the other three are filled in here.
		ADS.PitchConstant = 115.0f;   ADS.PitchDamping = 20.0f;
		ADS.YawConstant   = 95.0f;    ADS.YawDamping   = 15.0f;
		ADS.RollConstant  = 20000.0f; ADS.RollDamping  = 20.0f;

		// The R-301's hot HIP set is a copy of its cold one: hip-fire keeps recovering even through
		// a held burst, and only the aimed spring lets the pattern pile up. An LMG zeroes both
		// (lmg_vkp_hot: 0/35 hip, 0/45 ADS) — that is the knob for a weapon meant to walk off target.
		HipfireHot = Hipfire;

		ADSHot.PitchConstant = 0.0f;     ADSHot.PitchDamping = 45.0f;
		ADSHot.YawConstant   = 0.0f;     ADSHot.YawDamping   = 50.0f;
		ADSHot.RollConstant  = 20000.0f; ADSHot.RollDamping  = 20.0f;
	}

	/** Hip-fire, trigger not held long enough to heat. Apex R-301: pitch 40/20, yaw 45/20. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery|Springs")
	FRecoilSpringSet Hipfire;

	/** Aimed, cold. Stiffer than the hip set on purpose: the sight snaps back, the hip view wallows.
	 *  Apex R-301: pitch 115/20 (ratio 0.93), yaw 95/15 (ratio 0.77, so it swings past zero). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery|Springs")
	FRecoilSpringSet ADS;

	/** Hip-fire, trigger held. Constant 0 = the burst accumulates and nothing pulls it back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery|Springs")
	FRecoilSpringSet HipfireHot;

	/** Aimed, trigger held. Same idea; this is the set that makes a pattern worth learning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery|Springs")
	FRecoilSpringSet ADSHot;

	/** Share of each shot applied to the view INSTANTLY, the rest arriving through the spring.
	 *  Apex splits every axis into hardScale and softScale for exactly this; on the R-301 the hard
	 *  share works out around 0.14 of the pitch. Zero is all smooth and reads mushy, one is all
	 *  snap and reads like a hitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery|Delivery", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HardFraction = 0.15f;

	/** Heat added per shot. 1.0 (Apex's value on every weapon that has a hot set) means a single
	 *  shot heats the spring completely, so the difference between a tap and a burst is made by
	 *  how long the heat is HELD, not by how much of it there is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery|Heat", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeatPerShot = 1.0f;

	/** Seconds the heat holds after a shot before it starts fading. 0 = use the weapon's own refire
	 *  interval, which is what keeps a held trigger hot and a tap not (Apex hand-authors this to
	 *  about one interval: 0.08 on the R-301, 0.12 on the LMG). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery|Heat", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeatHoldTime = 0.0f;

	/** Seconds to fade from hot to cold once the hold has run out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery|Heat", meta = (ClampMin = "0.01", ClampMax = "2.0"))
	float HeatFadeTime = 0.05f;
};

/**
 * Cosmetic kick on the first-person weapon model. Never touches the camera; in ADS the fraction
 * is normally 0 so sights stay perfectly aligned while the camera carries all recoil.
 */
USTRUCT(BlueprintType)
struct FRecoilVisualKickSettings
{
	GENERATED_BODY()

	/** Enable visual weapon kick */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick")
	bool bEnableVisualKick = true;

	/** Fraction of recoil given to the weapon model instead of the camera when hip-firing.
	 *  Apex keeps this low (0.05 on the R-301, 0.15 on the Alternator): almost all of the kick is
	 *  real view movement, and very little is the gun waving where the bullets are not going. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HipfireWeaponFraction = 0.1f;

	/** Visual amplification of the weapon-model portion when hip-firing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick", meta = (ClampMin = "0.1", ClampMax = "15.0"))
	float HipfireVMScale = 1.0f;

	/** Fraction of recoil given to the weapon model when aiming (0 = all to camera). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ADSWeaponFraction = 0.0f;

	/** Visual amplification of the weapon-model portion when aiming. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick", meta = (ClampMin = "0.1", ClampMax = "15.0"))
	float ADSVMScale = 1.0f;

	/** Positional kickback along the barrel axis (cm). Purely visual. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float KickBackDistance = 3.0f;

	/** Minimum random roll per shot (deg) — twist around the barrel. Purely visual. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float RollRandomMin = 0.3f;

	/** Maximum random roll per shot (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float RollRandomMax = 0.5f;

	/** Multiplier on roll magnitude for snap feel (1.0 = honest degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float RollHardScale = 1.85f;

	/** Spring stiffness of the model kick recovery (higher = faster snap back to rest). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Kick", meta = (ClampMin = "20.0", ClampMax = "500.0"))
	float KickSpringStiffness = 150.0f;
};

/**
 * Weapon-model sway: mouse lag plus three Ornstein-Uhlenbeck noise layers (breathing, tremor,
 * micro-jitter). Cosmetic only, separate from the per-shot kick.
 */
USTRUCT(BlueprintType)
struct FRecoilSwaySettings
{
	GENERATED_BODY()

	/** Enable procedural weapon sway */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway")
	bool bEnableWeaponSway = true;

	/** Mouse movement sway intensity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float MouseSwayIntensity = 1.5f;

	/** Mouse sway lag (lower = more responsive) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway", meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float MouseSwayLag = 8.0f;

	/** Max mouse sway offset (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float MaxMouseSwayOffset = 3.0f;

	/** Movement sway intensity multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float MovementSwayMultiplier = 1.0f;

	/** Sway multiplier when aiming down sights (0 = no sway in ADS, 1 = full sway) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ADSSwayMultiplier = 0.3f;

	/** Enable stochastic organic sway (breathing + tremor + jitter layers) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway")
	bool bEnableOrganicSway = true;

	// --- Layer 1: Breathing (slow, large amplitude) ---

	/** How quickly breathing sway returns to center (higher = more centered, less drift) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway|Breathing", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float BreathingReversionSpeed = 0.8f;

	/** Magnitude of random breathing fluctuations (higher = more movement) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway|Breathing", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float BreathingVolatility = 0.5f;

	/** Maximum breathing sway deflection in degrees (hard clamp) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway|Breathing", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float BreathingMaxAngle = 0.4f;

	/** Per-axis scale for breathing: X=Pitch, Y=Yaw, Z=Roll */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway|Breathing")
	FVector BreathingAxisScale = FVector(1.0f, 0.6f, 0.3f);

	// --- Layer 2: Tremor (medium speed, medium amplitude) ---

	/** How quickly tremor returns to center */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway|Tremor", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float TremorReversionSpeed = 3.0f;

	/** Magnitude of random tremor fluctuations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway|Tremor", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float TremorVolatility = 0.3f;

	/** Maximum tremor deflection in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway|Tremor", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float TremorMaxAngle = 0.15f;

	/** Per-axis scale for tremor: X=Pitch, Y=Yaw, Z=Roll */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway|Tremor")
	FVector TremorAxisScale = FVector(1.0f, 0.7f, 0.5f);

	// --- Layer 3: Micro-jitter (fast, tiny amplitude) ---

	/** How quickly micro-jitter returns to center */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway|MicroJitter", meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float JitterReversionSpeed = 8.0f;

	/** Magnitude of random micro-jitter fluctuations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway|MicroJitter", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float JitterVolatility = 0.15f;

	/** Maximum micro-jitter deflection in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway|MicroJitter", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float JitterMaxAngle = 0.06f;

	/** Per-axis scale for jitter: X=Pitch, Y=Yaw, Z=Roll (typically no roll for jitter) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway|MicroJitter")
	FVector JitterAxisScale = FVector(1.0f, 1.0f, 0.0f);
};

/**
 * Apex-style recoil smoothing: while the player's view is moving fast (tracking a target,
 * strafing across one), vertical kick is heavily reduced. Rewards keeping the crosshair on the
 * target instead of punishing it. Measured from raw look input, so recoil itself cannot feed
 * back into the measurement.
 */
USTRUCT(BlueprintType)
struct FRecoilSmoothingSettings
{
	GENERATED_BODY()

	/** Enable recoil smoothing while tracking */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing")
	bool bEnableRecoilSmoothing = true;

	/** Look speed (deg/sec) below which no reduction happens — flicks slower than this eat full recoil. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float MinViewSpeed = 60.0f;

	/** Look speed (deg/sec) at which reduction reaches its maximum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float MaxViewSpeed = 240.0f;

	/** Recoil multiplier at full smoothing (0.15 = 85% of vertical kick removed while tracking). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinMultiplier = 0.15f;

	/** How quickly the smoothing multiplier chases its target (higher = snappier transitions). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float InterpSpeed = 10.0f;
};

/**
 * Posture multipliers stacked on top of the pattern.
 */
USTRUCT(BlueprintType)
struct FRecoilSituationalSettings
{
	GENERATED_BODY()

	/** Recoil multiplier when in air */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multipliers", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float AirborneMultiplier = 1.5f;

	/** Recoil multiplier when crouching */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multipliers", meta = (ClampMin = "0.3", ClampMax = "1.0"))
	float CrouchMultiplier = 0.7f;

	/** Recoil multiplier when aiming down sights */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multipliers", meta = (ClampMin = "0.3", ClampMax = "1.0"))
	float ADSMultiplier = 0.6f;

	/** Recoil multiplier when moving */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multipliers", meta = (ClampMin = "0.8", ClampMax = "2.0"))
	float MovingMultiplier = 1.2f;
};

/**
 * Complete recoil settings for a weapon, grouped by concern.
 */
USTRUCT(BlueprintType)
struct FWeaponRecoilSettings
{
	GENERATED_BODY()

	/** Where the aim point travels over a burst (curve asset, sampled by burst time) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
	FRecoilPatternSettings Pattern;

	/** Recentering after the burst + camera delivery spring */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
	FRecoilRecoverySettings Recovery;

	/** Cosmetic kick on the weapon model (never affects aim) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
	FRecoilVisualKickSettings VisualKick;

	/** Cosmetic weapon-model sway */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
	FRecoilSwaySettings Sway;

	/** Tracking reward: reduced kick while the view is moving fast */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
	FRecoilSmoothingSettings Smoothing;

	/** Posture multipliers (air / crouch / ADS / moving) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
	FRecoilSituationalSettings Situational;
};

/**
 * Weapon recoil, Apex Legends-style:
 * - Fixed learnable pattern authored as a CurveVector (shot number -> pitch/yaw + scatter radius)
 * - The pattern plays in both stances; the stances differ by spring, model share and spread
 * - Constant shot magnitude (no escalation)
 * - Recoil is a SEPARATE LAYER (PunchOffset), not a write into the control rotation — see below
 * - Six springs (pitch/yaw/roll x hip/ADS) plus hot variants; the spring is the only thing that
 *   returns the view, so there is no recovery speed and no recovery delay anywhere here
 * - Recoil smoothing: fast view motion suppresses kick
 * - Spring-damper visual weapon kick + Ornstein-Uhlenbeck organic sway (cosmetic)
 *
 * THE LAYER. Every shot moves PunchOffset, never the player's own aim. What the player is pointing
 * at is control rotation + PunchOffset, summed in exactly two places: AShooterCharacter::GetAimRay
 * (so bullets go where the kicked view points — this is real recoil, not a visual effect) and
 * APolarityCameraManager (so the picture matches). The springs pull PunchOffset back toward zero
 * and know nothing about the mouse.
 *
 * That separation is the whole point, and it deletes a pile of special cases. Recoil and aim are
 * no longer the same number, so nothing has to guess whether a degree of pitch came from the
 * weapon or from the player: pulling down moves the aim, the punch unwinds on its own, and if a
 * player counters a burst perfectly they finish below where they started. Apex and Counter-Strike
 * both behave this way. The previous model wrote recoil into the controller and then tried to take
 * it back by bookkeeping, which needed a hand-tuned heuristic to notice manual pull-down, only
 * worked on pitch, only downward, and only inside a window after the burst.
 */
UCLASS(ClassGroup = (Weapon), meta = (BlueprintSpawnableComponent))
class POLARITY_API UWeaponRecoilComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponRecoilComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==================== Setup ====================

	/** Initialize with references */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void Initialize(APlayerController* InController, UCharacterMovementComponent* InMovement, UApexMovementComponent* InApexMovement = nullptr);

	/** Set recoil settings (usually from weapon) */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void SetRecoilSettings(const FWeaponRecoilSettings& InSettings);

	// ==================== Firing Events ====================

	/** Called when weapon fires. ShotIntervalSeconds = the weapon's current refire rate; used to
	 *  decide when a pause becomes a new burst (falls back to 0.3 s when unknown). */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void OnWeaponFired(float ShotIntervalSeconds = 0.0f);

	/** Fire a single shot using override recoil settings (ignores current weapon settings and burst state) */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void FireWithOverrideSettings(const FWeaponRecoilSettings& OverrideSettings);

	/** Called when firing stops */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void OnFiringEnded();

	/** Reset all recoil state */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void ResetRecoil();

	// ==================== Input ====================

	/** Feed mouse input for sway calculation and recoil-smoothing measurement */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void AddMouseInput(float DeltaYaw, float DeltaPitch);

	// ==================== Getters ====================

	/** Get current visual weapon offset */
	UFUNCTION(BlueprintPure, Category = "Recoil")
	FVector GetWeaponOffset() const { return CurrentWeaponOffset; }

	/** Kick and sway, summed. Kept as the one-call answer for callers that want "everything the
	 *  recoil component does to the weapon's rotation" and do not care which half is which. */
	UFUNCTION(BlueprintPure, Category = "Recoil")
	FRotator GetWeaponRotationOffset() const { return CurrentWeaponRotation + CurrentSwayOffset; }

	/** Per-shot spring kick only, no sway. Split out from GetWeaponRotationOffset because ADS
	 *  scales the two differently: the kick is already divided between camera and weapon by
	 *  ADSWeaponFraction, while sway is scaled by ADSSwayMultiplier. A caller that re-applies
	 *  them after the ADS sight alignment has to weigh them separately. */
	UFUNCTION(BlueprintPure, Category = "Recoil")
	FRotator GetWeaponKickRotation() const { return CurrentWeaponRotation; }

	/** Sway only (mouse lag + organic breathing/tremor/jitter), no per-shot kick. */
	UFUNCTION(BlueprintPure, Category = "Recoil")
	FRotator GetWeaponSwayRotation() const { return CurrentSwayOffset; }

	/** The recoil layer, in degrees away from where the player is actually aiming (pitch up+,
	 *  yaw right+, roll). Added to the control rotation by AShooterCharacter::GetAimRay and to the
	 *  POV by APolarityCameraManager, and by nobody else: two readers, one number. */
	UFUNCTION(BlueprintPure, Category = "Recoil")
	FRotator GetPunchRotation() const { return PunchOffset; }

	/** 0 = spring cold (a shot springs back on its own), 1 = hot (the burst piles up and stays). */
	UFUNCTION(BlueprintPure, Category = "Recoil")
	float GetSpringHeat() const { return SpringHeat; }

	// ==================== State Setters ====================

	/** Set ADS state for recoil reduction */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void SetAiming(bool bAiming) { bIsAiming = bAiming; }

	/** Set crouching state for recoil reduction */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void SetCrouching(bool bCrouching) { bIsCrouching = bCrouching; }

	/** Set external sway override multiplier (e.g. charge launcher shaking). 1.0 = normal. */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void SetSwayOverrideMultiplier(float Multiplier) { SwayOverrideMultiplier = Multiplier; }

	/** Set an external recoil scalar applied on top of the situational multipliers (e.g. the ADS
	 *  time-dilation upgrade reducing recoil while active). 1.0 = normal, <1.0 = reduced recoil.
	 *  Clamped to >= 0. Affects the aim-kick (pitch/yaw). */
	UFUNCTION(BlueprintCallable, Category = "Recoil")
	void SetExternalRecoilMultiplier(float Multiplier) { ExternalRecoilMultiplier = FMath::Max(0.0f, Multiplier); }

public:
	// ==================== Editor Seeding ====================

	/** Fill a recoil pattern curve from key data (X = shot number, Y = pitch deg, Z = yaw deg).
	 *  ScatterRadii fills the curve's third channel, one entry per key, in degrees; pass an empty
	 *  array to leave the scatter at zero. It is a separate array rather than a second overload
	 *  because UHT does not allow overloaded UFUNCTIONs. Editor-only convenience: UCurveVector's
	 *  keys are not writable from Python in 5.x, so tuning scripts seed curves through this
	 *  instead. No-op outside the editor. */
	UFUNCTION(BlueprintCallable, Category = "Recoil|Editor")
	static void SeedRecoilCurve(UCurveVector* Curve, const TArray<FVector>& Keys, const TArray<float>& ScatterRadii);

protected:
	// ==================== Settings ====================

	UPROPERTY()
	FWeaponRecoilSettings Settings;

	// ==================== References ====================

	UPROPERTY()
	TObjectPtr<APlayerController> OwnerController;

	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> MovementComponent;

	UPROPERTY()
	TObjectPtr<UApexMovementComponent> ApexMovement;

	// ==================== Recoil State ====================

	/** Shots fired so far in the current burst — the pattern curve's X axis. Shot 0 reads key 0. */
	int32 ShotsInBurst = 0;

	/** The recoil layer itself: degrees the view is kicked away from where the player is aiming.
	 *  Mirrored from the three springs every frame, read by GetPunchRotation. */
	FRotator PunchOffset = FRotator::ZeroRotator;

	/** 0 cold, 1 hot. Rises by HeatPerShot on every shot, holds, then fades; blends each axis'
	 *  spring between its cold and hot set, which is what decides whether a burst piles up. */
	float SpringHeat = 0.0f;

	/** Time since last shot (for heat hold and burst reset) */
	float TimeSinceLastShot = 0.0f;

	/** Refire interval reported by the weapon on the last shot (burst-reset window and the default
	 *  heat hold time both derive from it) */
	float LastShotInterval = 0.0f;

	/** Is currently firing */
	bool bIsFiring = false;

	// ==================== Recoil Smoothing State ====================

	/** Exponentially smoothed look speed (deg/sec) from raw mouse input */
	float SmoothedLookSpeed = 0.0f;

	/** Current suppression multiplier on shot recoil (1 = none, MinMultiplier = full tracking) */
	float CurrentSmoothingMultiplier = 1.0f;

	// ==================== Visual Kick State (Spring-Damper) ====================

	/** Current weapon position offset (read by ShooterCharacter) */
	FVector CurrentWeaponOffset = FVector::ZeroVector;

	/** Per-shot spring kick ONLY. Sway lives in CurrentSwayOffset and is summed in at the getter,
	 *  not here. Written every frame by UpdateVisualKick, which owns it. */
	FRotator CurrentWeaponRotation = FRotator::ZeroRotator;

	/** Spring states for each visual kick axis */
	FBobSpringState KickSpringPitch;
	FBobSpringState KickSpringYaw;
	FBobSpringState KickSpringRoll;
	FBobSpringState KickSpringBack;

	// ==================== Punch Spring State ====================
	// Three springs holding PunchOffset. Target is always zero: the kick puts them away from it,
	// and their own restoring force is the entire recovery system.

	FBobSpringState CameraRecoilSpringPitch;
	FBobSpringState CameraRecoilSpringYaw;
	FBobSpringState CameraRecoilSpringRoll;

	// ==================== Weapon Sway State ====================

	/** Current mouse velocity (for sway) */
	FVector2D CurrentMouseVelocity = FVector2D::ZeroVector;

	/** Smoothed mouse velocity */
	FVector2D SmoothedMouseVelocity = FVector2D::ZeroVector;

	/** Current sway offset */
	FRotator CurrentSwayOffset = FRotator::ZeroRotator;

	// ==================== Ornstein-Uhlenbeck Sway State ====================
	// 3 layers x 3 axes (Pitch/Yaw/Roll) = 9 independent O-U processes

	float BreathingOU[3] = {0.0f, 0.0f, 0.0f};
	float TremorOU[3] = {0.0f, 0.0f, 0.0f};
	float JitterOU[3] = {0.0f, 0.0f, 0.0f};

	/** Deterministic random stream: seeded per instance so a given burst replays identically */
	FRandomStream RecoilRandomStream;

	// ==================== Character State ====================

	bool bIsAiming = false;
	bool bIsCrouching = false;
	float SwayOverrideMultiplier = 1.0f;

	/** External recoil scalar applied on top of situational multipliers (1.0 = no change). Set via SetExternalRecoilMultiplier. */
	float ExternalRecoilMultiplier = 1.0f;

	// ==================== Internal Methods ====================

	/** Calculate recoil for current shot */
	FRotator CalculateShotRecoil();

	/** Get situational recoil multiplier */
	float GetSituationalMultiplier() const;

	/** Check if character is airborne */
	bool IsAirborne() const;

	/** Check if character is moving */
	bool IsMoving() const;

	/** Put one shot into the punch springs: HardFraction lands on the offset immediately, the rest
	 *  arrives as a velocity impulse sized so the spring peaks at exactly the authored degrees. */
	void QueuePunch(const FRotator& Recoil);

	/** Advance the three punch springs toward zero and refresh PunchOffset. */
	void UpdatePunchSprings(float DeltaTime);

	/** Advance the cold/hot blend from the time since the last shot. */
	void UpdateSpringHeat(float DeltaTime);

	/** The spring set in force this frame for the current stance, cold and hot blended by heat. */
	FRecoilSpringSet ResolveActiveSpringSet() const;

	/** Seconds the heat holds after a shot: HeatHoldTime, or the weapon's refire interval at 0. */
	float ResolveHeatHoldTime() const;


	/** Update visual weapon kick (spring-damper) */
	void UpdateVisualKick(float DeltaTime);

	/** Update weapon sway */
	void UpdateWeaponSway(float DeltaTime);

	/** Trigger visual kick from recoil-derived viewmodel portion + independent roll */
	void TriggerVisualKick(const FRotator& ViewmodelRecoil, float RollKick);

	/** Advance a single Ornstein-Uhlenbeck process by one tick */
	float AdvanceOU(float CurrentValue, float ReversionSpeed, float Volatility, float MaxAngle, float DeltaTime);

	/** Update the tracking-based recoil suppression multiplier from measured look speed */
	void UpdateSmoothingMultiplier(float DeltaTime);

	/** Pause length (seconds) after which the next shot starts a new burst */
	float GetBurstResetThreshold() const;
};
