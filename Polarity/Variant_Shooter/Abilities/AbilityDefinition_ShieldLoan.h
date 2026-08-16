// AbilityDefinition_ShieldLoan.h
// Borrow an enemy's shield: pledge a slice of it now in exchange for a dash, collect on the next hit.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition.h"
#include "AbilityDefinition_ShieldLoan.generated.h"

USTRUCT(BlueprintType)
struct FShieldLoanLevelStats
{
	GENERATED_BODY()

	// ==== Common (mirrored from FAbilityCommonStats) ====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0", ClampMax = "60.0", Units = "s"))
	float Cooldown = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0"))
	float MinimumChargeToActivate = 0.0f;

	// ==== Ability-specific ====

	/** Fraction of the enemy's CURRENT shield to pledge. Taken from what is actually there rather
	 *  than a flat number, so the loan is worth more against an enemy the team has already worked
	 *  on, which is the same "reward the group's progress" shape the Tank's active has. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loan", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LoanFraction = 0.5f;

	/** Dash speed given in exchange, per point of shield pledged. The design ties the distance to the
	 *  size of the loan: a bigger debt buys a longer approach. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loan", meta = (ClampMin = "0.0"))
	float DashSpeedPerPledged = 120.0f;

	/** Ceiling on that, so a heavily charged enemy does not fling the caster across the level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loan", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaxDashSpeed = 2200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loan", meta = (ClampMin = "100.0", Units = "cm"))
	float Range = 3000.0f;
};

/**
 * The Melee class's active, in the design's words: borrow the enemy's shield.
 *
 * It takes nothing off the enemy now. It marks a slice as owed, and hands the caster the distance to
 * come and collect it -- so the ability is an opening move rather than damage, and the payoff only
 * exists if the swing actually lands.
 */
UCLASS(BlueprintType)
class POLARITY_API UAbilityDefinition_ShieldLoan : public UAbilityDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loan|Levels", meta = (TitleProperty = "LoanFraction"))
	TArray<FShieldLoanLevelStats> Levels;

	virtual int32 GetMaxLevel() const override { return Levels.Num(); }
	virtual FAbilityCommonStats GetCommonStatsAtLevel(int32 Level) const override;

	UFUNCTION(BlueprintPure, Category = "Loan|Levels")
	FShieldLoanLevelStats GetStatsAtLevel(int32 Level) const;
};
