// AIAccuracyComponent.h
// NPC accuracy system based on target speed

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AIAccuracyComponent.generated.h"

class UCurveFloat;
class ACharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSuppressionStart, float, Duration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSuppressionEnd);

/** How used to a fight this shooter is. Picks one of the three tunings on UAIAccuracyComponent. */
UENUM(BlueprintType)
enum class EAIExperience : uint8
{
	Rookie,
	Regular,
	Veteran
};

/** The numbers that make a rookie a rookie and a veteran a veteran. */
USTRUCT(BlueprintType)
struct FAIExperienceTuning
{
	GENERATED_BODY()

	FAIExperienceTuning() = default;
	FAIExperienceTuning(float InReaction, float InAimedShare, float InHitStill, float InHitDodging, float InStressGain)
		: ReactionSeconds(InReaction), AimedShare(InAimedShare), HitChanceStill(InHitStill)
		, HitChanceDodging(InHitDodging), StressGainScale(InStressGain)
	{
	}

	/** Seconds the target has to be in view before a round may be meant to hit it. Until then the
	 *  trigger may go down, but every round misses on purpose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experience", meta = (ClampMin = "0.0", Units = "s"))
	float ReactionSeconds = 0.35f;

	/** Share of the rounds that are aimed, once reacted. The rest pass close by on purpose: they are
	 *  there to frighten and pin, not to kill. Spread evenly through the string of fire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experience", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AimedShare = 0.6f;

	/** Chance an aimed round lands, point-blank, on a target not moving across the line of fire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experience", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HitChanceStill = 0.85f;

	/** ...and on one moving across it at DodgeReferenceSpeed or faster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experience", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HitChanceDodging = 0.63f;

	/** Every stress event counts this much of itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experience", meta = (ClampMin = "0.0"))
	float StressGainScale = 0.8f;
};

/**
 * Component that calculates NPC aim accuracy based on target's movement speed.
 * Faster targets are harder to hit. Wall running provides additional spread bonus.
 * Supports suppression state: forced donut-pattern misses around the target.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class POLARITY_API UAIAccuracyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAIAccuracyComponent();

	// ==================== Settings ====================

	/** Base spread when target is stationary (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float BaseSpread = 2.0f;

	/** Maximum spread at max target speed (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float MaxSpread = 20.0f;

	/**
	 * Curve mapping normalized speed (0-1) to spread multiplier (0-1).
	 * X = target speed / MaxTargetSpeed
	 * Y = spread interpolation factor
	 * If null, linear interpolation is used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy")
	TObjectPtr<UCurveFloat> SpeedToSpreadCurve;

	/** Target speed considered maximum for normalization (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy", meta = (ClampMin = "100.0"))
	float MaxTargetSpeed = 1200.0f;

	/**
	 * Distribution curve for shot placement within the spread cone.
	 * X = random value (0-1)
	 * Y = distance from center (0 = center, 1 = edge of cone)
	 * Use to bias shots toward center or edges.
	 * If null, uniform distribution is used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy")
	TObjectPtr<UCurveFloat> SpreadDistributionCurve;

	/** Additional spread multiplier when target is wall running */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Modifiers", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float WallRunSpreadMultiplier = 1.3f;

	/** Additional spread multiplier when target is in the air (jumping/falling) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Modifiers", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float InAirSpreadMultiplier = 1.2f;

	// ==================== Shooter's own movement ====================
	// Everything above asks how hard the TARGET is to hit. This asks how badly this NPC is shooting
	// because of what IT is doing, and the two multiply rather than compete: a sprinting enemy firing
	// at a wall-running player is worse at both jobs at once.
	//
	// It exists because enemies now fire while moving. Without a penalty, an enemy closing the
	// distance at full speed is strictly better than one holding still, and the push turns into a
	// charge that simply melts whoever it picked. The penalty is what makes moving fire read as
	// pressure rather than damage.

	/** Spread multiplier at ReferenceSpeed and above. 1.0 disables the penalty entirely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Self Movement", meta = (ClampMin = "1.0"))
	float SelfMovementSpreadMultiplier = 1.6f;

	/** Own 2D speed (cm/s) at which the full multiplier applies. Below it the penalty ramps in
	 *  linearly, so a walking enemy is only slightly worse and a sprinting one is much worse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Self Movement", meta = (ClampMin = "1.0"))
	float SelfMovementReferenceSpeed = 600.0f;

	// ==================== Suppression Settings ====================

	/** Minimum deviation angle during suppression — guarantees a miss (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Suppression", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float MinSuppressionSpread = 8.0f;

	/** Maximum deviation angle during suppression — keeps shots visually near the target (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Suppression", meta = (ClampMin = "5.0", ClampMax = "45.0"))
	float MaxSuppressionSpread = 20.0f;

	/** Past this range the cone stops opening: the spread in degrees is scaled down so that the
	 *  miss at the target, in cm, stays what it is at this range (cm). 0: off, the cone is the same
	 *  number of degrees at any range, which is how it was.
	 *
	 *  The spread numbers were set on arenas fought at 10 to 15 m. An outpost is fought at 30 to 60,
	 *  and there the same cone is a circle 7 m across: measured 2026-10-11, 3 percent of rounds hit at
	 *  25 to 35 m, 1.4 at 35 to 45, none past 45, and a player standing in the open in front of four
	 *  or five guns at 31 to 51 m lived 11.6, 16.2 and 31.6 s.
	 *
	 *  The number wanted [author, 2026-10-11]: about 10 s for a player standing in full view of five
	 *  at 40 m. From the three measurements that is twice the hits, so the cone at 40 m 0.7 of what
	 *  it is: 30 m. Nearer than this nothing changes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Base Spread", meta = (ClampMin = "0.0", Units = "cm"))
	float SpreadFullRange = 3000.0f;

	// ==================== Suppressive Fire Settings ====================
	// The OTHER half of suppression, and the two must not be confused. Everything above is about an
	// NPC that IS being suppressed: somebody is shooting at it and its aim falls apart. This is an
	// NPC that CHOOSES to suppress: it aims to miss close, so that the target stays behind cover
	// while a teammate moves. Same donut pattern, different reason and a tighter ring, because a
	// deliberate pin should read as "those rounds are for me" rather than as bad shooting.

	/** Minimum deviation while firing suppressive (degrees). Above zero, so the shots miss. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Suppressive Fire", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float MinSuppressiveFireSpread = 3.0f;

	/** Maximum deviation while firing suppressive (degrees). Keep it small: rounds have to land near
	 *  enough to be read as aimed at the target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Suppressive Fire", meta = (ClampMin = "1.0", ClampMax = "45.0"))
	float MaxSuppressiveFireSpread = 9.0f;

	// ==================== Settling on a target ====================
	// How the shooter gives ground to whoever it shoots at. A fresh string of fire starts loose and
	// the spread tightens the longer the string runs. So a target that keeps breaking the line is shot
	// at badly, and one that stands in the open is shot at well. The warning round that used to open
	// every string is the reaction below now: everything fired before it has run out misses.

	/** Spread multiplier at the start of a string of fire. 1 disables the ramp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Settling", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float UnsettledSpreadMultiplier = 3.0f;

	/** Seconds of sustained fire at one target until the multiplier above is gone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Settling", meta = (ClampMin = "0.0", Units = "s"))
	float AimSettleSeconds = 1.5f;

	/** A pause in the fire longer than this starts a new string. Above the burst cooldown, or every
	 *  burst would be a new string. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Settling", meta = (ClampMin = "0.0", Units = "s"))
	float AimSettleResetSeconds = 2.5f;

	/** A deliberate miss is not allowed to hit anything this close to the shooter (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Settling", meta = (ClampMin = "0.0"))
	float OwnCoverClearDistance = 400.0f;

	/** How much the current string of fire is widening the spread. 1 when settled. */
	UFUNCTION(BlueprintPure, Category = "Accuracy|Settling")
	float GetSettleSpreadMultiplier() const;

	/** The gun is held on Target without firing: watching the place it went down behind. The string
	 *  goes on settling, so the one who pops up where he went down is met by a settled aim and not
	 *  by a first round that misses on purpose [author, 2026-10-11: a player crouched behind cover,
	 *  came up every few seconds and was shot at as if seen for the first time each time]. */
	void NoteAiming(AActor* Target);

	// ==================== Pin fire ====================

	/** Shoot near the target instead of at it: this NPC has no aimed-fire slot from the coordinator
	 *  (AAICombatCoordinator::RequestAimedFire), so it keeps the target under fire without adding to
	 *  the damage. Same ring as suppressive fire, a flag of its own because that one has other owners
	 *  (the squad's withdraw role, the tree's task) and two owners of one flag switch each other off. */
	void SetPinFire(bool bEnabled) { bPinFire = bEnabled; }

	bool IsPinFire() const { return bPinFire; }

	// ==================== Fire discipline: reaction, aimed rounds, stress ====================
	// [author, 2026-10-14] Up close the cone above is a few centimetres wide and every round of an
	// automatic landed: the player died in half a second at four metres. The gun still fires at its
	// own rate, but only some of the rounds are meant to hit:
	//   - none until the target has been in view for the reaction time;
	//   - after that AimedShare of the rounds are aimed, the rest pass close by on purpose;
	//   - an aimed round lands with a chance rolled for it (HitChanceStill / HitChanceDodging, less
	//     with range, the shooter moving, the target in the air, a light in the eyes), and lands on
	//     the body; a failed roll passes close by like the rest. The cone above is not used.
	//   - stress (being suppressed, hit, wounded, a friend dying near) worsens all of it.
	// Damage per magazine is then known before the fight: rounds x AimedShare x chance x damage.
	// Regular, AKX203 (20 x 20), point-blank, calm: 0.6 x 0.85 = 204 standing, 0.6 x 0.63 = 151 dodging.
	// Only shooters that report what they see (UpdateSight) are held to this; the rest shoot as before.

	/** Which tuning this shooter uses. Spawners and blueprints set it; Regular when nobody did. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline")
	EAIExperience Experience = EAIExperience::Regular;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline")
	FAIExperienceTuning RookieTuning = { 0.5f, 0.45f, 0.75f, 0.5f, 1.0f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline")
	FAIExperienceTuning RegularTuning = { 0.35f, 0.6f, 0.85f, 0.63f, 0.8f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline")
	FAIExperienceTuning VeteranTuning = { 0.25f, 0.75f, 0.9f, 0.7f, 0.6f };

	/** The share of each round wanders this much either way, so aimed rounds do not fall on a beat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float AimedRoundJitter = 0.3f;

	/** Speed across the line of fire (cm/s) at which the target counts as fully dodging. Running
	 *  straight at the shooter or away from it is not dodging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline", meta = (ClampMin = "1.0", Units = "cm/s"))
	float DodgeReferenceSpeed = 400.0f;

	/** Up to this range the hit chance is whole (cm)... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline", meta = (ClampMin = "0.0", Units = "cm"))
	float HitChanceFullRange = 800.0f;

	/** ...and from this range on it is FarHitChanceScale of it (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline", meta = (ClampMin = "0.0", Units = "cm"))
	float HitChanceFarRange = 4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FarHitChanceScale = 0.35f;

	/** A landing round goes this far at most from the point aimed at (cm): inside the torso. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline", meta = (ClampMin = "0.0", Units = "cm"))
	float HitPlacementRadius = 18.0f;

	// ==================== Readiness ====================
	// [author, 2026-10-14] At arm's length the one who was ready wins, not the better shot. A rush
	// into a room that heard it coming meets guns already on the door; a quiet entry from a side
	// nobody watches meets soldiers that have to turn and react. What a new sight costs:
	//   - calm (nothing heard for AlertSeconds): the experience's reaction;
	//   - alert (heard, hit, called, saw somebody): AlertReactionScale of it;
	//   - holding (alert within HoldFreshSeconds, and the eyes within HoldConeDegrees of where he
	//     shows): HoldReactionSeconds, and then every round of FirstVolleySeconds is aimed;
	//   - not holding, the eyes elsewhere: up to TurnReactionSeconds more, at 90 degrees off and past.

	/** Alert this long after the last thing that alerted it (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Readiness", meta = (ClampMin = "0.0", Units = "s"))
	float AlertSeconds = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Readiness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AlertReactionScale = 0.7f;

	/** Within this angle of where its eyes are it is holding on him (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Readiness", meta = (ClampMin = "0.0", ClampMax = "90.0", Units = "deg"))
	float HoldConeDegrees = 15.0f;

	/** Reaction of one holding the place he shows at (s). Stress still lengthens it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Readiness", meta = (ClampMin = "0.0", Units = "s"))
	float HoldReactionSeconds = 0.1f;

	/** Holding wears off: this long after the last thing that alerted it, or the gun last held on
	 *  him, it is only alert (s). Make a noise, wait, come in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Readiness", meta = (ClampMin = "0.0", Units = "s"))
	float HoldFreshSeconds = 5.0f;

	/** One holding the place: every round this long after its reaction is aimed (s). At 0.5 a very
	 *  skilled player sliding into two regulars holding the door wins about one time in four
	 *  (simulated 2026-10-14; 0.3 gives a half). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Readiness", meta = (ClampMin = "0.0", Units = "s"))
	float FirstVolleySeconds = 0.5f;

	/** Added to the reaction when he shows 90 degrees or more off where its eyes are (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Readiness", meta = (ClampMin = "0.0", Units = "s"))
	float TurnReactionSeconds = 0.3f;

	// ==================== Headache (stress grenade) ====================
	// Through walls and over a wide area. While it lasts: stress no lower than its strength, the
	// reaction longer still, no holding and no first volley, no calling to its side, and a friend
	// falling near freezes it for a moment.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Headache", meta = (ClampMin = "1.0"))
	float HeadacheReactionMultiplier = 1.6f;

	/** A friend falling within StressAllyDeathRadius freezes one with a headache this long (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Headache", meta = (ClampMin = "0.0", Units = "s"))
	float AllyDeathFlinchSeconds = 0.6f;

	/** Out of view for less than this and the reaction is not started over (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline", meta = (ClampMin = "0.0", Units = "s"))
	float SightLossResetSeconds = 1.0f;

	/** The gun was held on the place the target went down behind (NoteAiming): the reaction to it
	 *  coming up there again is this share of the usual. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WatchedReactionScale = 0.5f;

	/** A round that misses on purpose passes this far outside the target's body at the least (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline", meta = (ClampMin = "0.0", Units = "cm"))
	float MissClearanceMin = 40.0f;

	/** Added to the capsule before the clearance: the arms and the gun reach out of the capsule, and a
	 *  round is a projectile with a body of its own. Without it a fifth of the hits at 2.6 m came from
	 *  rounds meant to miss, most of them into the arms (CORNER bench, 2026-10-14). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline", meta = (ClampMin = "0.0", Units = "cm"))
	float MissBodyMargin = 45.0f;

	/** ...and this far at the most (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline", meta = (ClampMin = "0.0", Units = "cm"))
	float MissClearanceMax = 120.0f;

	/** Reaction multiplier while blinded by a tactical light. The reaction also starts over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Discipline", meta = (ClampMin = "1.0"))
	float DazzleReactionMultiplier = 2.5f;

	/** Stress added when this shooter gets suppressed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Stress", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StressFromSuppression = 0.5f;

	/** Stress added per hit taken. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Stress", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StressFromHit = 0.25f;

	/** Stress added when somebody of its own side dies within StressAllyDeathRadius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Stress", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StressFromAllyDeath = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Stress", meta = (ClampMin = "0.0", Units = "cm"))
	float StressAllyDeathRadius = 1500.0f;

	/** The stress from events stays whole this long after the last of them (s)... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Stress", meta = (ClampMin = "0.0", Units = "s"))
	float StressHoldSeconds = 3.0f;

	/** ...then fades to nothing over this long (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Stress", meta = (ClampMin = "0.01", Units = "s"))
	float StressFadeSeconds = 12.0f;

	/** Share of the lost health that is stress for good: wounds do not fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Stress", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StressFromWounds = 0.6f;

	/** At full stress the share of aimed rounds is this much of the calm one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Stress", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StressAimedShareMultiplier = 0.6f;

	/** At full stress an aimed round lands this much as often. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Stress", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StressHitChanceMultiplier = 0.8f;

	/** At full stress a reload keeps the trigger up this many times as long. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Stress", meta = (ClampMin = "1.0"))
	float StressReloadMultiplier = 1.5f;

	/** At full stress the reaction takes this many times as long. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Stress", meta = (ClampMin = "1.0"))
	float StressReactionMultiplier = 1.5f;

	/** What this shooter sees of the target it is fighting, reported a few times a second by its
	 *  owner. The first call also puts the shooter under fire discipline. */
	void UpdateSight(AActor* Target, bool bInView);

	/** Where the round of the shot about to be aimed leaves from (the muzzle), when that is not the
	 *  eye: a round meant to miss has to clear the body from there too. Set before each aim. */
	void SetShotOrigin(const FVector& Origin) { ShotOrigin = Origin; bHasShotOrigin = true; }

	/** This shooter reports what it sees and is held to the reaction and the aimed rounds. */
	bool IsUnderFireDiscipline() const { return bFireDiscipline; }

	/** Has Target been in view long enough for a round to be meant to hit it. Always true for a
	 *  shooter that is not under fire discipline. */
	bool IsReactionDone(const AActor* Target) const;

	/** The reaction this shooter has right now: experience, stress, a light in its eyes. */
	float GetReactionSeconds() const;

	/** A stress event. Scaled by the experience's StressGainScale. */
	void AddStress(float Amount);

	/** How much of its health the owner has lost, 0 to 1. */
	void SetWoundFraction(float Fraction) { WoundFraction = FMath::Clamp(Fraction, 0.0f, 1.0f); }

	/** 0 calm, 1 as bad as it gets. */
	UFUNCTION(BlueprintPure, Category = "Accuracy|Stress")
	float GetStress() const;

	/** How many times the usual a reload keeps this shooter's trigger up. */
	float GetReloadTimeMultiplier() const { return FMath::Lerp(1.0f, StressReloadMultiplier, GetStress()); }

	/** The last round from CalculateAimDirection was meant to miss: not reacted yet, between aimed
	 *  rounds, suppressed, or pinning. For the record of the fight. */
	bool WasLastRoundDeliberateMiss() const { return bLastRoundDeliberateMiss; }

	/** The last round was placed, to land or to miss: the weapon must not scatter it further. */
	bool WasLastRoundPlaced() const { return bLastRoundDeliberateMiss || bLastRoundPlaced; }

	UFUNCTION(BlueprintCallable, Category = "Accuracy|Discipline")
	void SetExperience(EAIExperience InExperience) { Experience = InExperience; }

	const FAIExperienceTuning& GetTuning() const;

	/** Something alerted it: a noise heard, a hit, a shout, an enemy in view. @see AlertSeconds */
	void NoteAlert();

	bool IsAlert() const;

	/** A headache of Strength (0 to 1) for Seconds. Breaks the hold and the reaction it had. */
	void ApplyHeadache(float Strength, float Seconds);

	bool HasHeadache() const;

	/** Whatever it had seen of the target, the reaction to it starts over. */
	void RestartReaction() { LastSightTime = -1000.0f; }

	/** Every soldier within Radius of Center gets a headache, walls or not. Returns how many. Server. */
	UFUNCTION(BlueprintCallable, Category = "Accuracy|Headache", meta = (WorldContext = "WorldContext"))
	static int32 ApplyHeadacheInRadius(const UObject* WorldContext, FVector Center, float Radius, float Strength, float Seconds);

	// ==================== Runtime State ====================

	/** Last calculated spread value (for debugging) */
	UPROPERTY(BlueprintReadOnly, Category = "Accuracy|Debug")
	float LastCalculatedSpread = 0.0f;

	/** Last target speed ratio (for debugging) */
	UPROPERTY(BlueprintReadOnly, Category = "Accuracy|Debug")
	float LastSpeedRatio = 0.0f;

	// ==================== Suppression Delegates ====================

	/** Broadcast when suppression begins. Duration = total suppression time. */
	UPROPERTY(BlueprintAssignable, Category = "Accuracy|Suppression")
	FOnSuppressionStart OnSuppressionStart;

	/** Broadcast when suppression ends (timer expired or manually cleared). */
	UPROPERTY(BlueprintAssignable, Category = "Accuracy|Suppression")
	FOnSuppressionEnd OnSuppressionEnd;

	// ==================== API ====================

	/**
	 * Calculate aim direction with accuracy spread applied.
	 * During suppression, uses donut pattern instead of normal spread.
	 * @param TargetLocation - World location to aim at
	 * @param Target - Target actor (used to get velocity and movement state)
	 * @param ExtraMissDegrees - A round meant to miss stays this much further out: the half-angle of a
	 *        pellet pattern, which is laid around this direction after it is chosen.
	 * @return Direction vector with spread applied
	 */
	UFUNCTION(BlueprintCallable, Category = "Accuracy")
	FVector CalculateAimDirection(const FVector& TargetLocation, AActor* Target, float ExtraMissDegrees = 0.0f);

	/**
	 * Get current effective spread for a target (degrees).
	 * Does not apply randomization, just returns the spread angle.
	 */
	UFUNCTION(BlueprintPure, Category = "Accuracy")
	float GetCurrentSpread(AActor* Target) const;

	/** How much this NPC's own movement is widening its spread right now. 1.0 when standing still. */
	UFUNCTION(BlueprintPure, Category = "Accuracy|Self Movement")
	float GetSelfMovementSpreadMultiplier() const;

	/**
	 * Get target's normalized speed (0-1 range).
	 */
	UFUNCTION(BlueprintPure, Category = "Accuracy")
	float GetTargetSpeedRatio(AActor* Target) const;

	/**
	 * Check if target is currently wall running.
	 */
	UFUNCTION(BlueprintPure, Category = "Accuracy")
	bool IsTargetWallRunning(AActor* Target) const;

	/**
	 * Check if target is currently in the air (jumping/falling).
	 */
	UFUNCTION(BlueprintPure, Category = "Accuracy")
	bool IsTargetInAir(AActor* Target) const;

	/**
	 * Apply spread to a direction vector.
	 * @param BaseDirection - Original aim direction (normalized)
	 * @param SpreadDegrees - Spread cone half-angle in degrees
	 * @return New direction with random offset within cone
	 */
	UFUNCTION(BlueprintCallable, Category = "Accuracy")
	FVector ApplySpreadToDirection(const FVector& BaseDirection, float SpreadDegrees) const;

	// ==================== Suppression API ====================

	/**
	 * Apply suppression: forces donut-pattern misses for the given duration.
	 * Stacks with diminishing returns if already suppressed.
	 * @param Duration - How long the suppression lasts (seconds)
	 * @param DiminishingFactor - Stacking falloff rate. Each stack adds Duration / (1 + StackCount * Factor)
	 */
	UFUNCTION(BlueprintCallable, Category = "Accuracy|Suppression")
	void ApplySuppression(float Duration, float DiminishingFactor = 0.5f);

	/** Immediately clear suppression state */
	UFUNCTION(BlueprintCallable, Category = "Accuracy|Suppression")
	void ClearSuppression();

	/** Returns true if currently suppressed */
	UFUNCTION(BlueprintPure, Category = "Accuracy|Suppression")
	bool IsSuppressed() const { return bIsSuppressed; }

	// ==================== Painted by a laser [author, 2026-10-15] ====================

	/** Painted for this long after the last time a laser's dot was held on it or by it. */
	void NotePainted(float Seconds);

	bool IsPainted() const;

	/** Seconds it has been painted without a break (0 when it is not). */
	float GetPaintedSeconds() const;

	/** A reason to stay down behind its cover: rounds coming past, or a laser on it. */
	bool IsPinnedDown() const { return bIsSuppressed || IsPainted(); }

	/** Painted this long without a break, it gives the corner up for another one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Painted", meta = (ClampMin = "0.0", Units = "s"))
	float PaintedRelocateSeconds = 2.0f;

	/** Returns remaining suppression time (0 if not suppressed) */
	UFUNCTION(BlueprintPure, Category = "Accuracy|Suppression")
	float GetSuppressionTimeRemaining() const { return SuppressionTimeRemaining; }

	// ==================== Suppressive Fire API ====================

	/** Turn deliberate suppressive fire on or off.
	 *
	 *  A mode rather than a timed effect on purpose: it lasts exactly as long as the behaviour that
	 *  wants it, and the behaviour is the only thing that knows when the teammate has finished
	 *  moving. Whoever switches it on owns switching it off. */
	UFUNCTION(BlueprintCallable, Category = "Accuracy|Suppressive Fire")
	void SetSuppressiveFire(bool bEnabled);

	/** True while this NPC is deliberately shooting to pin rather than to kill. */
	UFUNCTION(BlueprintPure, Category = "Accuracy|Suppressive Fire")
	bool IsFiringSuppressive() const { return bSuppressiveFire; }

	// ==================== Dazzle ====================

	/** Spread multiplier while this NPC is blinded by a tactical light (AShooterNPC::ApplyDazzle).
	 *  1 = not dazzled. Set and cleared by the NPC, which owns how long the dazzle lasts. Also the
	 *  device side of the reaction: a light in the eyes stretches it and starts it over. */
	void SetDazzleSpreadMultiplier(float Multiplier);

	float GetDazzleSpreadMultiplier() const { return DazzleSpreadMultiplier; }

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Get aim origin (owner's location, typically eye height) */
	FVector GetAimOrigin() const;

	/** Sample the spread distribution curve */
	float SampleSpreadDistribution() const;

	/**
	 * Apply donut-shaped spread: shots land between MinSpread and MaxSpread degrees from center.
	 * Guarantees a miss (minimum deviation) while keeping shots visually close to the target.
	 */
	FVector ApplyDonutSpread(const FVector& BaseDirection, float InnerSpread, float OuterSpread) const;

	/** ApplyDonutSpread for the misses that are MEANT (pin fire, the warning round, suppressive fire):
	 *  the same ring, but a miss that would land on something within OwnCoverClearDistance of the
	 *  shooter is rolled again. A round meant to pass the target's ear must not go into the corner the
	 *  shooter is leaning round, which is where a few degrees off puts it [author, 2026-10-11]. */
	FVector ApplyDonutSpreadClearOfOwnCover(const FVector& BaseDirection, float InnerSpread, float OuterSpread) const;

	/** A round that misses on purpose, measured in centimetres at the target and not in degrees: a
	 *  ring of degrees is a few centimetres wide at two metres and lands in the body. Passes
	 *  MissClearanceMin..Max outside the target's capsule, never into the shooter's own cover. */
	FVector MakeDeliberateMiss(const FVector& TargetLocation, const AActor* Target, float ExtraMissDegrees) const;

	/** Direction as it is when its round passes clear of a pawn target, a MakeDeliberateMiss when it
	 *  would land in him. For the rings in degrees (suppressed, pinning), which hit at close range. */
	FVector KeepClearOfBody(const FVector& Direction, const FVector& TargetLocation, const AActor* Target, float ExtraMissDegrees) const;

	/** How far from the body's axis a round meant to miss has to pass at the least. */
	float GetMissMustClear(float Range, float BodyRadius, float ExtraMissDegrees) const;

	/** The closest the round's lines (from the eye, from the muzzle) come to the body's axis. */
	float GetMissPassDistance(const FVector& Direction, float Range, const FVector& AxisTop, const FVector& AxisBottom) const;

private:
	/** True when NPC is in suppression state (forced misses) */
	bool bIsSuppressed = false;

	/** True while this NPC is deliberately firing to pin somebody. Runtime intent set by behaviour,
	 *  never authored on the component, which is why it is not a UPROPERTY. */
	bool bSuppressiveFire = false;

	/** Seconds remaining in current suppression */
	float SuppressionTimeRemaining = 0.0f;

	/** Number of suppression applications during current suppression (for diminishing returns) */
	int32 SuppressionStackCount = 0;

	/** @see SetDazzleSpreadMultiplier */
	float DazzleSpreadMultiplier = 1.0f;

	/** @see SetPinFire */
	bool bPinFire = false;

	/** The current string of fire: who at, when it started, when the last round of it was aimed. */
	TWeakObjectPtr<AActor> SettleTarget;
	float SettleStartTime = -1000.0f;
	float LastAimTime = -1000.0f;

	/** @see UpdateSight. Only a shooter that reports what it sees is held to the discipline. */
	bool bFireDiscipline = false;

	/** Whom the sight below is about, when it came into view, when it was last in view. */
	TWeakObjectPtr<AActor> SightTarget;
	float SightSince = -1000.0f;
	float LastSightTime = -1000.0f;

	/** The reaction the current sight started with: decided when it starts, so the watched-corner
	 *  discount and a light in the eyes apply to that one. */
	float SightReaction = 0.0f;

	/** Last time the gun was held on the place the target went down behind. */
	float LastWatchTime = -1000.0f;

	/** The first aimed round of this sight has gone: written to the record once. */
	bool bReactionAnnounced = false;

	/** Last thing that alerted it. @see NoteAlert */
	float LastAlertTime = -1000.0f;

	/** The current sight started on one holding the place: rounds aimed until then. */
	float VolleyUntil = -1000.0f;

	/** How the current sight started, for the record: 0 calm, 1 alert, 2 holding; and how far off
	 *  the eyes it was (degrees). */
	uint8 SightReadiness = 0;
	float SightAngle = 0.0f;

	float HeadacheStrength = 0.0f;
	float HeadacheUntil = -1000.0f;

	/** Shares of a round gathered towards the next aimed one: at 1 a round is aimed. Starts full on
	 *  every new sight, so the first round after the reaction is aimed. */
	float AimedCredit = 1.0f;

	/** The chance an aimed round at Target lands right now. @see FAIExperienceTuning::HitChanceStill */
	float GetHitChance(const AActor* Target, float Range) const;

	/** Towards a point on the body around TargetLocation: a round that lands. */
	FVector MakeHit(const FVector& TargetLocation, float Range) const;

	/** Stress from events at the time of the last one, and when that was. */
	float EventStress = 0.0f;
	float LastStressEventTime = -1000.0f;

	float WoundFraction = 0.0f;

	/** Above 1 while a light is in its eyes. */
	float DeviceReactionMultiplier = 1.0f;

	bool bLastRoundDeliberateMiss = false;
	bool bLastRoundPlaced = false;

	float PaintedUntil = -1000.0f;
	float PaintedSince = -1000.0f;

	/** @see SetShotOrigin */
	FVector ShotOrigin = FVector::ZeroVector;
	bool bHasShotOrigin = false;

	/** The reaction for a sight starting now. */
	float ComputeReactionForNewSight(float Now, const AActor* Target, uint8& OutReadiness, float& OutAngle) const;
};