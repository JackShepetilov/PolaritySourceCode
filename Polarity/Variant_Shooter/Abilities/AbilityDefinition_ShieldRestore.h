// AbilityDefinition_ShieldRestore.h
// Give an enemy its shield back, and be healed for exactly what you handed over.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition.h"
#include "AbilityDefinition_ShieldRestore.generated.h"

USTRUCT(BlueprintType)
struct FShieldRestoreLevelStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0", ClampMax = "60.0", Units = "s"))
	float Cooldown = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0"))
	float MinimumChargeToActivate = 0.0f;

	/** Most shield this can hand back in one cast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Restore", meta = (ClampMin = "0.0"))
	float MaxShieldRestored = 8.0f;

	/** Health returned to the caster per point of shield actually handed over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Restore", meta = (ClampMin = "0.0"))
	float HealPerShieldRestored = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Restore", meta = (ClampMin = "100.0", Units = "cm"))
	float Range = 4000.0f;
};

/**
 * The Tank's active, and the design is explicit that it is antisocial on purpose: it undoes the
 * team's work on one enemy and pays the Tank for doing it. That is a deliberate bet on the genre,
 * not a bug to balance away.
 *
 * The incentive falls out of the arithmetic rather than needing to be tracked: you can only give
 * back shield that was taken, so a fresh enemy is worth nothing and one the team has nearly stripped
 * is worth the most. The Tank is pushed to pick exactly the target that will annoy everyone.
 */
UCLASS(BlueprintType)
class POLARITY_API UAbilityDefinition_ShieldRestore : public UAbilityDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Restore|Levels", meta = (TitleProperty = "MaxShieldRestored"))
	TArray<FShieldRestoreLevelStats> Levels;

	virtual int32 GetMaxLevel() const override { return Levels.Num(); }
	virtual FAbilityCommonStats GetCommonStatsAtLevel(int32 Level) const override;

	UFUNCTION(BlueprintPure, Category = "Restore|Levels")
	FShieldRestoreLevelStats GetStatsAtLevel(int32 Level) const;
};
