// AbilityDefinition_SniperPassive.h
// The Sniper's always-on ability: ground covered between shots becomes damage that ignores shields.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition.h"
#include "AbilityDefinition_SniperPassive.generated.h"

USTRUCT(BlueprintType)
struct FSniperPassiveLevelStats
{
	GENERATED_BODY()

	/** Ground the player has to cover between two shots to reach the full bonus.
	 *
	 *  Distance travelled, not time spent: standing still with the trigger off pays nothing, and the
	 *  class is paid for repositioning between shots rather than for waiting between them. Measured
	 *  as the path actually walked -- a player who circles back to where they started has still
	 *  covered it.
	 *
	 *  TEST VALUE, and the one that decides whether the mechanic is legible. Too small and the
	 *  readout snaps between its two ends and looks broken; the number wants to be big enough that a
	 *  player watches it climb. Roughly: sprint speed times the seconds you want between shots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Travel", meta = (ClampMin = "1.0", Units = "cm"))
	float DistanceForFullBonus = 4000.0f;

	/** Damage dealt by a shot fired without having moved at all since the previous one. Zero is the
	 *  honest floor: a Sniper who stands still gets nothing from this passive and shoots like anyone
	 *  else. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Travel", meta = (ClampMin = "0.0"))
	float DamageAtNoTravel = 0.0f;

	/** Damage dealt once DistanceForFullBonus has been covered. Everything past that distance is
	 *  worth nothing more, so the bonus has a stated ceiling instead of growing with a long walk
	 *  between engagements.
	 *
	 *  This damage is the passive's OWN, and it lands on health through a shield that is still up.
	 *  That is what the class is being given: an answer to a target nobody has stripped yet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Travel", meta = (ClampMin = "0.0"))
	float DamageAtFullTravel = 45.0f;
};

/**
 * The Sniper's passive: movement turned into shield-piercing damage.
 *
 * The accumulator fills with every centimetre the player covers and is spent by the shot that
 * follows, so the class is asked to keep relocating between shots rather than to hold one window.
 * It pairs with the grapple deliberately -- the active is what makes the ground cheap to cover, and
 * the passive is what the covering is worth.
 *
 * It deals its OWN damage rather than scaling the weapon's, and the difference is the whole point:
 * the weapon's damage waits for the target's shield to come off, and this does not. A multiplier on
 * a number that is being held at zero multiplies nothing.
 *
 * There is no cooldown and no charge cost here, and the inherited fields for both are ignored: this
 * is never activated. @see UAbilityComponent's passive channel.
 *
 * The player is told the number rather than left to infer it: the overhead indicator over an enemy
 * this Sniper has already shot shows what a shot right now would deal in total.
 * @see UAbilityHandler_SniperPassive::GetPredictedShotDamage.
 */
UCLASS(BlueprintType)
class POLARITY_API UAbilityDefinition_SniperPassive : public UAbilityDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Travel|Levels", meta = (TitleProperty = "DistanceForFullBonus"))
	TArray<FSniperPassiveLevelStats> Levels;

	/** How long an enemy stays on the list of targets whose damage readout this Sniper is shown,
	 *  counted from the last hit on it. Zero means forever, until it dies.
	 *
	 *  The list exists so the readout appears on enemies the player has engaged rather than on every
	 *  enemy in the room, which would be a screen of numbers rather than a lesson about one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Readout", meta = (ClampMin = "0.0", Units = "s"))
	float ReadoutMemorySeconds = 0.0f;

	virtual int32 GetMaxLevel() const override { return FMath::Max(1, Levels.Num()); }

	UFUNCTION(BlueprintPure, Category = "Travel|Levels")
	FSniperPassiveLevelStats GetStatsAtLevel(int32 Level) const;
};
