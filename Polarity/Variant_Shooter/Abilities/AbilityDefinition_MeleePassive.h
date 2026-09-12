// AbilityDefinition_MeleePassive.h
// The Melee class's always-on ability: the lunge reaches as far as the target's shield is gone.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition.h"
#include "AbilityDefinition_MeleePassive.generated.h"

USTRUCT(BlueprintType)
struct FMeleePassiveLevelStats
{
	GENERATED_BODY()

	/** Reach against an enemy whose shield is untouched, as a multiplier on the melee component's
	 *  own LungeRange.
	 *
	 *  A multiplier rather than a number of centimetres on purpose: the weapon owns what a swing is
	 *  worth, and retuning LungeRange on a machete has to retune this with it. A flat value here
	 *  would silently pin the passive to whatever the machete happened to be on the day it was
	 *  authored, and the two would drift apart without anything reporting it.
	 *
	 *  1.0 means "no different from anyone else", which is the honest floor: a full shield is the
	 *  state every enemy starts in, and the class is not supposed to open at extreme range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lunge", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float ReachMultiplierAtFullShield = 1.0f;

	/** Reach against an enemy whose shield is completely stripped.
	 *
	 *  This is the payoff the design asks for: the team works an enemy's shield down, and the Melee
	 *  can cross the room to finish it. Read off the enemy the same way the Tank's passive reads
	 *  "stripped" -- |charge| / MaxBaseCharge on its UEMFVelocityModifier -- so an enemy retuned to
	 *  a different charge cap retunes this too instead of quietly falling out of scale.
	 *
	 *  TEST VALUE. How far a room this actually crosses is a balance decision and is visible in the
	 *  first minute of play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lunge", meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float ReachMultiplierAtNoShield = 4.0f;

	// ==================== Charged jump out of one's own smoke ====================
	//
	// Hold jump while standing in a cloud YOU threw, and let go to launch. The smoke is the Melee's
	// own active, so this is a payoff for having used it and not a movement option the class simply
	// owns: away from his smoke he jumps like everybody else.
	//
	// The numbers live here rather than in UMovementSettings because they grow with the passive's
	// level, and the movement asset has no notion of levels. TEST VALUES, all of them.

	/** Upward speed at a full charge. A charge of nothing gives the ordinary JumpZVelocity, so a tap
	 *  inside the smoke stays an ordinary jump and the mechanic never eats an input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke Jump", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SmokeJumpMaxZVelocity = 1400.0f;

	/** How long the charge takes to fill. Holding longer adds nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke Jump", meta = (ClampMin = "0.05", ClampMax = "3.0", Units = "s"))
	float SmokeJumpMaxChargeTime = 0.55f;

	/** Horizontal speed added along the direction being pushed at a full charge, scaled down with the
	 *  charge. Zero makes the jump purely vertical. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke Jump", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SmokeJumpForwardBoost = 400.0f;

	/** Ground speed multiplier while charging. The wind-up is meant to be a commitment, and being
	 *  visibly slow inside your own smoke is the cost of the height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke Jump", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SmokeJumpChargeMoveScale = 0.35f;

	/** Seconds before another charged jump. Only a jump that actually came out charged pays it: a
	 *  tap, and a charge cancelled by a dash or a slide, leave it untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke Jump", meta = (ClampMin = "0.0", Units = "s"))
	float SmokeJumpCooldown = 6.0f;

	// ==================== Blocking with the blade while sliding ====================

	/** Half-angle of the arc the melee weapon covers while sliding, measured from where the character
	 *  is FACING, not from where he is going. Anything arriving inside it does no damage at all:
	 *  bullets, bolts, projectiles and the explosions they make.
	 *
	 *  Zero disables the block, which is what every other class has and what this class has before
	 *  the level that grants it. 180 would block everything including a shot in the back, and is
	 *  clamped away rather than trusted, because "block" that ignores where the blade is pointing is
	 *  not a block.
	 *
	 *  Facing rather than travel on purpose: the slide is aimed with the mouse like everything else,
	 *  so the player decides what he is covering, and sliding backwards out of a fight covers the
	 *  fight. TEST VALUE. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide Block", meta = (ClampMin = "0.0", ClampMax = "120.0", Units = "deg"))
	float SlideBlockHalfAngle = 60.0f;

	/** What FRACTION of a blocked hit still gets through. 0.35 means the blade eats 65% of it.
	 *
	 *  A fraction rather than an on/off block, and that is a balance decision, not a technical one:
	 *  a total block would make a slide straight at a shooter a free approach with no answer, and
	 *  would make the character immune to a kamikaze drone's blast by running at it. Taking a third
	 *  of it still rewards facing the fire without deleting the threat.
	 *
	 *  0 is a total block and is allowed, so the top level of the passive can be one if that is what
	 *  play asks for. 1 leaves the block cosmetic. TEST VALUE. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide Block", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SlideBlockDamageMultiplier = 0.35f;
};

/**
 * The Melee class's passive.
 *
 * What is deliberately NOT here: movement speed and wall running. Those are the character's own
 * numbers and they live in the UMovementSettings asset the class Blueprint points at -- the Melee
 * has his own, everybody else shares the base one. Duplicating them here would be a second source
 * of truth for the same value, and the two would disagree the first time one of them was edited.
 *
 * What is also not here: kinetic damage from momentum and the damage an enemy takes from being
 * slammed into a wall. Both already exist for every character (UMeleeAttackComponent's momentum
 * damage and AShooterNPC's wall slam) and the design lists them under this class because this class
 * is the one that can generate the speed, not because they are gated on it.
 *
 * So one effect is left, and it is the one nothing else does: the lunge reaching further into a
 * broken shield.
 */
UCLASS(BlueprintType)
class POLARITY_API UAbilityDefinition_MeleePassive : public UAbilityDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lunge|Levels", meta = (TitleProperty = "ReachMultiplierAtNoShield"))
	TArray<FMeleePassiveLevelStats> Levels;

	virtual int32 GetMaxLevel() const override { return FMath::Max(1, Levels.Num()); }

	UFUNCTION(BlueprintPure, Category = "Lunge|Levels")
	FMeleePassiveLevelStats GetStatsAtLevel(int32 Level) const;
};
