// AbilityDefinition_Berserk.h
// The Melee class's active: a window of armoured flanks, paid back as health for the damage dealt.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition.h"
#include "AbilityDefinition_Berserk.generated.h"

USTRUCT(BlueprintType)
struct FBerserkLevelStats
{
	GENERATED_BODY()

	// ==== Common (mirrored from FAbilityCommonStats) ====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0", ClampMax = "60.0", Units = "s"))
	float Cooldown = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0"))
	float MinimumChargeToActivate = 0.0f;

	// ==== The window ====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berserk", meta = (ClampMin = "0.5", Units = "s"))
	float Duration = 8.0f;

	/** Half-angle of the arc berserk does NOT cover, measured from where the player is facing.
	 *
	 *  The front is left open on purpose. The class already has an answer for what is in front of it:
	 *  the passive's blade block covers exactly that arc while sliding. Covering the front here too
	 *  would make the two effects the same effect twice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berserk", meta = (ClampMin = "0.0", ClampMax = "179.0", Units = "deg"))
	float FrontHalfAngle = 70.0f;

	/** What a hit from outside that arc is multiplied by. 0.6 means the back and the sides take 40%
	 *  less. Never 0: a class that cannot be shot from behind stops having a behind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berserk", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float FlankDamageMultiplier = 0.6f;

	// ==== The payout ====

	/** Share of the damage dealt during the window that comes back as health when it closes.
	 *
	 *  At the END rather than as lifesteal per hit, and that is the whole feel of the ability: the
	 *  player spends the window committed and gets paid for what they actually did, instead of being
	 *  topped up mid-fight and never having to leave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berserk", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float HealFraction = 0.35f;

	/** Ceiling on that, because the input is how much damage the player managed to deal and the
	 *  ability does not control that number. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berserk", meta = (ClampMin = "0.0"))
	float MaxHeal = 60.0f;

	// ==== Reaching a teammate ====

	/** How far a teammate can be and still be picked up by the cast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berserk|Ally", meta = (ClampMin = "0.0", Units = "cm"))
	float AllyRange = 1500.0f;

	/** How close to the crosshair that teammate has to be. Small: this is "the one I am looking at",
	 *  not "anybody roughly over there", and a wide cone would pick the wrong person in a group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berserk|Ally", meta = (ClampMin = "1.0", ClampMax = "60.0", Units = "deg"))
	float AllyAimHalfAngle = 12.0f;
};

/**
 * The Melee class's active, shaped after Conduit's first ability in how it is USED and nothing else:
 * look at a teammate and the cast covers both of you, look at nobody and it covers you alone. There
 * is no separate self-cast button, because "not looking at anybody" is already an unambiguous answer.
 *
 * What it grants is not a shield -- this game has none. It is a window in which the back and the
 * sides are armoured, and at the end of which the damage the wearer DEALT comes back as health.
 * The two halves are one idea: it is safe to turn your back on a fight only for as long as you are
 * spending that safety on hitting something.
 *
 * The state itself lives on AShooterCharacter, not here: it has to sit on the teammate too, and that
 * teammate is carrying somebody else's ability in their slot. @see AShooterCharacter::StartBerserk
 */
UCLASS(BlueprintType)
class POLARITY_API UAbilityDefinition_Berserk : public UAbilityDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berserk|Levels", meta = (TitleProperty = "Duration"))
	TArray<FBerserkLevelStats> Levels;

	virtual int32 GetMaxLevel() const override { return FMath::Max(1, Levels.Num()); }
	virtual FAbilityCommonStats GetCommonStatsAtLevel(int32 Level) const override;

	UFUNCTION(BlueprintPure, Category = "Berserk|Levels")
	FBerserkLevelStats GetStatsAtLevel(int32 Level) const;
};
