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
