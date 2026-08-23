// AbilityDefinition_Grapple.h
// A line you swing on, not a shove toward what you are looking at.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition.h"
#include "AbilityDefinition_Grapple.generated.h"

class UMaterialInterface;
class UNiagaraSystem;

/**
 * Where the drawn line leaves the character.
 *
 * A look setting, not a physics one: the swing is anchored at the capsule wherever the rope appears
 * to start from. Chest is what both reference games do -- the line leaves the torso just under the
 * eyeline, so it stays in frame while the player looks around and never has to agree with whatever
 * the arms are doing. Hand is the old behaviour, kept because a weapon-mounted hook is a legitimate
 * look and because the third-person body really does have a hand where it appears to.
 */
UENUM(BlueprintType)
enum class EGrappleLineOrigin : uint8
{
	/** From the hand socket, and from a hand-shaped offset off the owner's camera. */
	Hand   UMETA(DisplayName = "Hand"),
	/** From the chest socket, and from just under the owner's camera. Titanfall's. */
	Chest  UMETA(DisplayName = "Chest (under the camera)")
};

/**
 * One level of the grapple.
 *
 * These are the numbers of a STEERABLE WINCH, which is what the reference actually is, and they came
 * out of the shipped Apex binary rather than out of anybody's guess: the ConVar table carries the
 * developers' own one-line descriptions of what each one does, and those descriptions are quoted
 * against the fields below so that nobody has to take this file's word for anything.
 *
 * An earlier version of this struct described a PENDULUM -- a rope of fixed length, gravity at full
 * strength, sideways speed conserved forever -- and every one of those three is wrong about the
 * reference. There is no rope: no length, no constraint, nothing of the kind exists in the
 * reference's tuning. There is a pull toward the point, a brake on everything that does not point
 * at it, and a gravity switch operated by angle.
 *
 * @see Docs/Grapple_Reference_Apex_Titanfall.md -- sources, method, unit conversion, and the full
 *      list of where this implementation still departs from the reference on purpose.
 */
USTRUCT(BlueprintType)
struct FGrappleLevelStats
{
	GENERATED_BODY()

	/** Reference: GrappleUtilityCooldown, 1 second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0", ClampMax = "60.0", Units = "s"))
	float Cooldown = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0"))
	float MinimumChargeToActivate = 0.0f;

	/** How far the hook reaches. Reference: grapple_maxLength 1100 * 1.96 = 2156. The 2100 this
	 *  used to hold came from a fan document by a different route, which is a useful check on the
	 *  conversion factor rather than a coincidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple", meta = (ClampMin = "100.0", Units = "cm"))
	float Range = 2156.0f;

	/** Closer than this and the hook refuses, so it cannot be used as a free standing-still dash.
	 *  Zero because neither reference has such a rule; it is a design lever, not a fix. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple", meta = (ClampMin = "0.0", Units = "cm"))
	float MinAnchorDistance = 0.0f;

	/** How fast the hook itself flies out. The pull does not begin until it lands, which is what
	 *  gives the ability its opening beat and what the cable is drawn along.
	 *  Reference: grapple_shootVel 3000 * 1.96 = 5880. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple", meta = (ClampMin = "0.0", Units = "cm/s"))
	float HookTravelSpeed = 5880.0f;

	// ---- The pull ----
	//
	// These are Apex's own numbers, converted from Source units by k = 1.96, the ratio of this
	// project's gravity (980 * DefaultGravityScale 1.5 = 1470) to Apex's (sv_gravity 750). Gravity
	// is the right thing to convert by: the shape of a trajectory is set by v^2/g, so at matching
	// gravity and matching times every length, speed and acceleration scales by one number. The
	// conversion checks out against a value nobody derived that way: grapple_maxLength 1100 * 1.96
	// = 2156, and Range above was 2100, from a completely different source.
	//
	// @see Docs/Grapple_Reference_Apex_Titanfall.md for where each number came from.

	/** Acceleration toward the grapple point, in cm/s^2. (No Units metadata: the engine has no unit
	 *  for an acceleration and an unrecognised one is a hard UHT error.)
	 *
	 *  Reference: grapple_accel_human 1000, "Speed added per second from grapple, up to the
	 *  grapple_speedRamp* speed". Note what that says: this is not an open-ended force, it is how
	 *  fast the pull closes on a TARGET SPEED. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Pull", meta = (ClampMin = "0.0"))
	float PullAcceleration = 1960.0f;

	/** The target speed toward the point when the pull starts.
	 *  Reference: grapple_speedRampMin_human 50. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Pull", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SpeedRampMin = 98.0f;

	/** What that target has grown to after SpeedRampTime.
	 *  Reference: grapple_speedRampMax_human 800. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Pull", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SpeedRampMax = 1568.0f;

	/** Seconds from SpeedRampMin to SpeedRampMax. Reference: grapple_speedRampTime_human 1.5. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Pull", meta = (ClampMin = "0.0", Units = "s"))
	float SpeedRampTime = 1.5f;

	/** Dead time between the hook biting and the pull starting. Not a ramp: nothing happens at all
	 *  for this long, and that is what gives the attach its beat.
	 *  Reference: grapple_pullDelay_human 0.2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Pull", meta = (ClampMin = "0.0", Units = "s"))
	float PullDelay = 0.2f;

	/** How hard everything NOT pointing at the grapple point is braked, in cm/s^2.
	 *
	 *  Reference: grapple_decel_human 425, "Deceleration of player's speed that doesn't go toward
	 *  the grapple point". This one line is what separates the reference from a pendulum, and it is
	 *  the biggest thing this ability had wrong. A rope redirects sideways speed and keeps it
	 *  forever; Apex deletes it. What is left is not a swing on a line, it is a winch you steer, and
	 *  the arc comes from gravity plus this brake rather than from a constraint.
	 *
	 *  Zero turns the ability back into a free pendulum, which is a real and quite different feel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Pull", meta = (ClampMin = "0.0"))
	float LateralDeceleration = 833.0f;

	/** Whether the brake above leaves DOWNWARD speed alone.
	 *
	 *  Reference: grapple_dontFightGravity 1, "Ignores downward speed when applying deceleration, so
	 *  that gravity continues to pull you down". With this off the brake cancels falling as well as
	 *  swinging, and the grapple becomes a rigid rail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Pull")
	bool bDontFightGravity = true;

	/** How far ABOVE the hook the pull actually aims, so a grapple onto a ledge puts the player on
	 *  top of it instead of into the wall under it. Reference: grapple_lift 25. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Pull", meta = (ClampMin = "0.0", Units = "cm"))
	float Lift = 49.0f;

	/** Hard ceiling on total speed. ZERO MEANS NONE, and the reference has no such ceiling: the ramp
	 *  above already governs the speed toward the point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Pull", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaxSpeed = 0.0f;

	// ---- Gravity ----
	//
	// The part that was most wrong. In the reference gravity is not physics that happens to you
	// during a grapple, it is a tool switched on by angle, and most of the time it is off.

	/** Cosine of the angle, measured from STRAIGHT DOWN, past which gravity acts at all. 0 is
	 *  horizontal, 1 is straight down.
	 *
	 *  Reference: grapple_letGravityHelpCosAngle 0.8, "Don't ignore gravity when grappling downward
	 *  this much". Read the name: gravity is let in when it pulls the player where they were going
	 *  anyway. At 0.8 that is roughly the last 37 degrees before straight down, so on any ordinary
	 *  grapple, upward or level, gravity is simply OFF.
	 *
	 *  Set it to 0 for gravity on every grapple, which is the old pendulum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Gravity", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LetGravityHelpCosAngle = 0.8f;

	/** Gravity multiplier while the player pushes forward AND is looking well below the hook.
	 *
	 *  Reference: grapple_gravityPushUnderContribution 2, "Pushing forward while looking 'under' the
	 *  grapple point increases gravity this much". It is how a player asks to be swung UNDER a point
	 *  rather than reeled up to it, and with gravity otherwise off it is the only way to ask.
	 *
	 *  Set it to 0 to remove the mechanic entirely and have gravity depend on the line's angle only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Gravity", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float GravityPushUnderContribution = 2.0f;

	/** How far below the hook the player must be looking before "pushing under" counts, in degrees.
	 *
	 *  Without a margin this mechanic fires constantly and pressing W reads as a gravity switch,
	 *  which is exactly what happened: the first version asked only whether the hook was higher than
	 *  the aim, and on the ordinary case -- hook thrown up, then look level to fly -- that is true
	 *  the whole time. Worse, while looking straight at the hook the two are equal and noise decides,
	 *  so the same key toggled gravity on and off frame to frame.
	 *
	 *  The reference's own margin is not in any dump, so this number is ours. 25 degrees means the
	 *  player has to deliberately drop their aim below the hook to ask for the swing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Gravity", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float PushUnderMinAngleDegrees = 25.0f;

	// ---- The moment it bites ----

	/** Fraction of horizontal speed kept when the hook connects.
	 *  Reference: grapple_initialSlowFrac_human 1.0, so all of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Attach", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InitialSlowFracHorizontal = 1.0f;

	/** Fraction of VERTICAL speed kept when the hook connects.
	 *  Reference: grapple_initialSlowFracVert_human 0.4, so three fifths of a fall is deleted on
	 *  contact. This is most of why a grapple feels like it CATCHES you. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Attach", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InitialSlowFracVertical = 0.4f;

	/** Speed added along the line at the moment of contact.
	 *  Reference: grapple_initialImpulse_human 350. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Attach", meta = (ClampMin = "0.0", Units = "cm/s"))
	float InitialImpulse = 686.0f;

	/** Upward speed added when the hook connects while the player is standing on the floor.
	 *
	 *  Reference: grapple_initialImpulseOffGround_human 50, which is tiny next to the 420 this used
	 *  to be. It can afford to be: with gravity off the player does not need throwing clear of the
	 *  ground, they simply stop being pulled back down to it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Attach", meta = (ClampMin = "0.0", Units = "cm/s"))
	float InitialImpulseOffGround = 98.0f;

	/** Speed toward the point the player is snapped to on contact if they are slower than it.
	 *  Reference: grapple_initialSpeedMin_human 0, so off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Attach", meta = (ClampMin = "0.0", Units = "cm/s"))
	float InitialSpeedMin = 0.0f;

	// ---- Steering ----

	/** How hard the player's own WASD steers, as a Quake/Source air acceleration.
	 *
	 *  NOT from the reference. Apex uses its ordinary air movement during a grapple (sv_airaccelerate
	 *  10, player_extraairaccelleration 2.0) and the wish-speed cap that goes with it lives in player
	 *  settings files that are in no public dump. Rather than invent a number and call it fidelity,
	 *  this keeps the project's own steering. @see Docs/Grapple_Reference_Apex_Titanfall.md */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Steering", meta = (ClampMin = "0.0"))
	float SwingAirAcceleration = 5500.0f;

	/** The ceiling steering applies to the velocity component along the input direction. Also ours,
	 *  not the reference's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Steering", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SwingWishSpeed = 400.0f;

	// ---- The ground ----

	/** What is left of ground friction while a line is out and the character is touching the floor.
	 *
	 *  Not in the ConVar dump under any name, but the engine's walking friction is written to stop a
	 *  player who let go of the stick and it erases a grapple's speed in a tenth of a second.
	 *  Keeping it near zero is what makes a graze a skid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Ground", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GroundFrictionScale = 0.05f;

	/** The same, for the braking the engine applies when there is no input at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Ground", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GroundBrakingScale = 0.0f;

	// ---- Letting go ----

	/** Below this speed the line starts running out of patience.
	 *
	 *  The shipped binary keeps m_grappleSwingDetachLowSpeed, m_grappleHasGoodVelocity and
	 *  m_grappleLastGoodVelocityTime per player, so the reference rule is "let go when the player
	 *  stops going anywhere". The VALUES are in no dump, so these two numbers are ours.
	 *
	 *  This replaces release-by-angle. There is no quarter-turn rule anywhere in the reference:
	 *  not in the ConVars, not in the state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Release", meta = (ClampMin = "0.0", Units = "cm/s"))
	float DetachLowSpeed = 390.0f;

	/** How long the player may stay under DetachLowSpeed before the line lets go. A grace period, so
	 *  the bottom of an arc or a moment against a wall is not instantly fatal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Release", meta = (ClampMin = "0.0", Units = "s"))
	float DetachLowSpeedTime = 0.4f;

	/** Close enough to the aim point to let go automatically. A radius rather than a point because
	 *  two machines are never in exactly the same place, and a hard threshold is one they can land
	 *  on opposite sides of. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Release", meta = (ClampMin = "10.0", Units = "cm"))
	float ArrivalRadius = 150.0f;

	/** Longest one line may be held whatever the player does. OURS, not the reference's: Apex's base
	 *  grapple has grapple_power_use_rate 0, so it drains nothing, has no duration at all, and ends
	 *  only by arriving, by letting go, or by running out of speed. Kept as a safety net. Zero means
	 *  no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Release", meta = (ClampMin = "0.0", Units = "s"))
	float MaxDuration = 2.5f;

	// NOTE: there is no "momentum on release" setting, and there should not be one. The speed on the
	// clock when the line drops is kept in full, always, because that is the point of the mechanic in
	// both games it comes from -- releasing at the top of an arc is how a player banks speed, and a
	// fraction here would be a dial for taking that away.
};

/**
 * The Sniper's active: a line thrown at geometry, which the player then swings on.
 *
 * A steerable WINCH, which is what the reference is. Not a pendulum, and not by preference: the
 * model below was read out of the shipped Apex binary rather than guessed at.
 * @see Docs/Grapple_Reference_Apex_Titanfall.md
 *
 * Four things act at once and the ability is their interaction, not any one of them:
 *
 *   1. a PULL toward the hook (plus a small Lift, so a grapple onto a ledge puts you on top of it).
 *      It is an acceleration closing on a TARGET SPEED, and the target itself grows over a second
 *      and a half, after a dead delay in which nothing at all happens.
 *   2. a BRAKE on every part of the velocity that does NOT point at that aim point. This is the
 *      piece that makes it a winch: a rope conserves sideways speed forever, the reference deletes
 *      it, and the arc a player flies is what is left of gravity and this brake against the pull.
 *   3. GRAVITY, which is OFF unless the line points steeply downward, and doubled while the player
 *      pushes forward under the point. A player who says a grapple has no gravity in it is right.
 *   4. the player's own WASD, as Quake/Source air acceleration. Ours rather than the reference's:
 *      its air numbers live in files that are in no public dump.
 *
 * On release the velocity is left completely alone. All of it is kept.
 *
 * Three earlier versions are named here so nobody rebuilds them. One was a single LaunchCharacter
 * impulse: a dash with a rope drawn over it. One summed the direction to the anchor with the
 * camera's forward vector and used that as the pull, which was a winch aimed by the head with no arc
 * to it. One was an idealised pendulum on a fixed-length rope at full gravity with perfectly
 * conserved tangential speed -- a better toy than either, and still not what the reference does.
 *
 * The motion lives in UApexMovementComponent, inside the simulated move, because everything that
 * writes Velocity has to. This asset supplies the numbers and the look; the handler traces the
 * anchor, waits for the hook to land, and hands the movement component the intent.
 *
 * It feeds the class's own passive rather than standing alone -- that passive pays for distance
 * travelled between shots, so the line is how a Sniper earns its own damage. Moving is the class.
 *
 * Activation is expected to be Hold: press throws the line, release drops it. On Tap it still works,
 * and then only arriving, the duration, or running out of speed ever end it.
 */
UCLASS(BlueprintType)
class POLARITY_API UAbilityDefinition_Grapple : public UAbilityDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Levels", meta = (TitleProperty = "Range"))
	TArray<FGrappleLevelStats> Levels;

	// ==================== The weapon ====================
	// On the ability rather than on FGrappleLevelStats, because neither of these is a stat: an
	// upgrade that makes the line longer or the pull harder has no business also changing how fast
	// the character puts a gun away. They belong beside the cable and the sockets, with the other
	// things that describe how the ability LOOKS at every level.

	/** Whether the held weapon is put away for the length of the grapple and drawn again after.
	 *
	 *  Both hands are on the line, so the gun goes away. It also takes firing with it: the stow uses
	 *  the same phase machine as a weapon swap, and everything that is off during a swap is off here
	 *  for the same reason. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Weapon")
	bool bStowWeapon = true;

	/** How much faster than usual the weapon is put away and brought back for a grapple.
	 *
	 *  A swap is paced to read as a deliberate choice; this is not a choice, it is the character's
	 *  hands being needed somewhere else, and at the authored pace the animation would still be
	 *  finishing as the grapple ends. Multiplies the weapon's OWN holster and draw play rates, so a
	 *  weapon with a slow, heavy draw stays slower than a light one: it is a scale on the authored
	 *  timing rather than a fixed duration replacing it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Weapon", meta = (ClampMin = "0.1", ClampMax = "8.0"))
	float WeaponStowSpeedMultiplier = 2.0f;

	// ==================== Look ====================

	/** Material on the cable that draws the line. Left empty draws the cable in the engine default,
	 *  which is visible but ugly -- it is a reminder to author one, not a shipping state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Look")
	TObjectPtr<UMaterialInterface> CableMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Look", meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float CableWidth = 2.0f;

	/** Segments in the cable. More sags more smoothly and costs more.
	 *
	 *  Changing this at runtime is handled, but it is not free: UCableComponent allocates its
	 *  particle array once, in OnRegister, so the cable has to be re-registered when this changes.
	 *  @see AShooterCharacter::EnsureGrappleCable, where the crash from getting that wrong is
	 *  written down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Look", meta = (ClampMin = "1", ClampMax = "64"))
	int32 CableSegments = 32;

	/** Slack in the line, as a fraction of the distance it spans. Zero draws it dead straight, which
	 *  reads as a beam rather than a rope; a little sag is what makes it look like one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Look", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float CableSlack = 0.02f;

	/** Where the line leaves the character. @see EGrappleLineOrigin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Look")
	EGrappleLineOrigin LineOrigin = EGrappleLineOrigin::Chest;

	/** Socket the line leaves from on the third-person body, which is what teammates see. The player
	 *  holding it gets their end from the camera instead, because the first-person mesh is not where
	 *  it appears to be. @see AShooterCharacter::GetGrappleHandLocation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Look", meta = (EditCondition = "LineOrigin == EGrappleLineOrigin::Hand", EditConditionHides))
	FName HandSocket = FName("hand_l");

	/** The same, for the chest origin. The third-person body has a real spine bone under the head,
	 *  so teammates see the line leave the torso rather than a hand that is busy holding a rifle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Look", meta = (EditCondition = "LineOrigin == EGrappleLineOrigin::Chest", EditConditionHides))
	FName ChestSocket = FName("spine_03");

	/** Burst played at the anchor when the hook bites, on every machine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Look")
	TObjectPtr<UNiagaraSystem> AttachVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Look")
	TObjectPtr<USoundBase> AttachSound;

	/** Played where the line lets go. Optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Look")
	TObjectPtr<USoundBase> DetachSound;

	virtual int32 GetMaxLevel() const override { return FMath::Max(1, Levels.Num()); }
	virtual FAbilityCommonStats GetCommonStatsAtLevel(int32 Level) const override;

	UFUNCTION(BlueprintPure, Category = "Grapple|Levels")
	FGrappleLevelStats GetStatsAtLevel(int32 Level) const;
};
