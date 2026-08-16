// AbilityDefinition_Grapple.h
// Pull yourself to where you are looking.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition.h"
#include "AbilityDefinition_Grapple.generated.h"

USTRUCT(BlueprintType)
struct FGrappleLevelStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0", ClampMax = "60.0", Units = "s"))
	float Cooldown = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0"))
	float MinimumChargeToActivate = 0.0f;

	/** How far the hook reaches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple", meta = (ClampMin = "100.0", Units = "cm"))
	float Range = 5000.0f;

	/** Travel speed toward the anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple", meta = (ClampMin = "100.0", Units = "cm/s"))
	float PullSpeed = 2600.0f;

	/** Upward kick added to the pull, so a hook onto a ledge clears its lip instead of dragging the
	 *  character into the wall below it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple", meta = (ClampMin = "0.0", Units = "cm/s"))
	float UpwardBoost = 500.0f;

	/** Closer than this and the hook refuses, so it cannot be used as a free standing-still dash. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple", meta = (ClampMin = "0.0", Units = "cm"))
	float MinAnchorDistance = 400.0f;
};

/**
 * The Sniper's active: a hook that pulls the caster to what they are aiming at.
 *
 * It feeds the class's own passive rather than standing alone -- that passive pays for distance
 * travelled between shots, so the hook is how a Sniper earns its own damage. Moving is the class,
 * and this is the tool for moving.
 */
UCLASS(BlueprintType)
class POLARITY_API UAbilityDefinition_Grapple : public UAbilityDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Levels", meta = (TitleProperty = "Range"))
	TArray<FGrappleLevelStats> Levels;

	virtual int32 GetMaxLevel() const override { return Levels.Num(); }
	virtual FAbilityCommonStats GetCommonStatsAtLevel(int32 Level) const override;

	UFUNCTION(BlueprintPure, Category = "Grapple|Levels")
	FGrappleLevelStats GetStatsAtLevel(int32 Level) const;
};
