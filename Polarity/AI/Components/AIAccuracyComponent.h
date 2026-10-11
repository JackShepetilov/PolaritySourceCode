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
	// How the shooter gives ground to whoever it shoots at. A fresh string of fire opens with a round
	// that misses on purpose (the warning: "you are being shot at, move") and starts loose; the spread
	// tightens the longer the string runs. So a target that keeps breaking the line is shot at badly,
	// and one that stands in the open is shot at well.

	/** The first round of a fresh string of fire misses on purpose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Settling")
	bool bFirstShotMisses = true;

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
	 * @return Direction vector with spread applied
	 */
	UFUNCTION(BlueprintCallable, Category = "Accuracy")
	FVector CalculateAimDirection(const FVector& TargetLocation, AActor* Target);

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
	 *  1 = not dazzled. Set and cleared by the NPC, which owns how long the dazzle lasts. */
	void SetDazzleSpreadMultiplier(float Multiplier) { DazzleSpreadMultiplier = FMath::Max(1.0f, Multiplier); }

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
};