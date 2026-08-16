// AbilityDefinition_ShieldBypass.h
// Open one enemy up: its shield stops absorbing, and it slows down while the window lasts.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition.h"
#include "AbilityDefinition_ShieldBypass.generated.h"

/**
 * Per-level stats. Common values are mirrored here so a designer sees everything about a level in
 * one struct, which is the shape UAbilityDefinition_Burst already established.
 */
USTRUCT(BlueprintType)
struct FShieldBypassLevelStats
{
	GENERATED_BODY()

	// ==== Common (mirrored from FAbilityCommonStats) ====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0", ClampMax = "60.0", Units = "s"))
	float Cooldown = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0"))
	float MinimumChargeToActivate = 0.0f;

	// ==== Ability-specific ====

	/** How long the enemy stays open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass", meta = (ClampMin = "0.1", Units = "s"))
	float Duration = 4.0f;

	/** Movement speed while open, as a fraction of normal. The slow is what makes the window usable
	 *  by the rest of the team rather than only by the caster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MoveSpeedMultiplier = 0.5f;

	/** Health damage per point of the enemy's charge, converted on impact. The bolt cashes in the
	 *  shield the team has already built up, so this is the exchange rate between their work and the
	 *  kill. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass", meta = (ClampMin = "0.0"))
	float RedirectDamageMultiplier = 6.0f;

	/** Flight speed of the bolt. Slow enough to be seen leaving, fast enough not to be waited on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass", meta = (ClampMin = "100.0", Units = "cm/s"))
	float ProjectileSpeed = 3000.0f;

	/** How far the caster can reach to pick a target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass", meta = (ClampMin = "100.0", Units = "cm"))
	float Range = 4000.0f;
};

/**
 * The Wizard's active, in the design's words: skip the opening phase.
 *
 * Time to kill is normally two phases that tune independently -- strip the shield, then execute --
 * and this collapses the first one on a single enemy for a few seconds. It deliberately does no
 * damage of its own: it changes what everyone ELSE's damage does, which is what makes it a
 * cooperative button rather than a personal one.
 */
UCLASS(BlueprintType)
class POLARITY_API UAbilityDefinition_ShieldBypass : public UAbilityDefinition
{
	GENERATED_BODY()

public:
	/** The bolt to fire. Left unset means the ability does nothing, which is louder than silently
	 *  falling back to an instant hit and is the correct failure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass")
	TSubclassOf<class AShieldBypassProjectile> ProjectileClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass|Levels", meta = (TitleProperty = "Duration"))
	TArray<FShieldBypassLevelStats> Levels;

	virtual int32 GetMaxLevel() const override { return Levels.Num(); }
	virtual FAbilityCommonStats GetCommonStatsAtLevel(int32 Level) const override;

	/** Level is 1-based, clamped to the authored range. Returns a default-constructed struct when
	 *  nothing is authored, so an unconfigured asset behaves harmlessly instead of crashing. */
	UFUNCTION(BlueprintPure, Category = "Bypass|Levels")
	FShieldBypassLevelStats GetStatsAtLevel(int32 Level) const;
};
