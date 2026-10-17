// RunLevelDefinition.h
// The run level: one ladder, the same for every player, climbed by each player on his own
// [author, 2026-10-16]. Docs/GameDesign_Tactical_To_PowerFantasy_2026-10-11.md section 11.
//
// Experience comes from kills, and a kill pays everyone who hurt that enemy (UKillCreditSubsystem).
// A step says what the player has from that level on: so many gadgets of one kind, so much of the
// suppression ignored. Each value carries on to the next steps until a step sets it again.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "RunLevelDefinition.generated.h"

/** One level of the ladder. Steps[0] is level 1. */
USTRUCT(BlueprintType)
struct FRunLevelStep
{
	GENERATED_BODY()

	/** Experience in all, from the start of the run, that reaches this level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Level", meta = (ClampMin = "1"))
	int32 XPToReach = 3;

	/** Gadgets of one kind from this level on. 0: as at the level before. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Level", meta = (ClampMin = "0"))
	int32 GadgetsPerKind = 0;

	/** How much of the suppression the player ignores from this level on: 0 takes it all, 1 none.
	 *  Negative: as at the level before. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Level", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float SuppressionResistance = -1.0f;
};

UCLASS(BlueprintType)
class POLARITY_API URunLevelDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	/** Experience every player who hurt an enemy gets when it dies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Level", meta = (ClampMin = "0"))
	int32 XPPerKill = 1;

	/** The ladder, level 1 first. XPToReach has to grow from step to step. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Level")
	TArray<FRunLevelStep> Steps;

	/** The level this much experience has reached. 0 below the first step. */
	int32 GetLevelForXP(int32 XP) const;

	/** Experience in all for the level after Level; -1 at the top. */
	int32 GetXPToReach(int32 NextLevel) const;

	/** Gadgets of one kind at Level; 0 when no step up to it says. */
	int32 GetGadgetsPerKind(int32 Level) const;

	/** Suppression ignored at Level, 0..1; 0 when no step up to it says. */
	float GetSuppressionResistance(int32 Level) const;
};

/** Project Settings -> Polarity -> Run Level: which ladder the game uses. */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Run Level"))
class POLARITY_API URunLevelSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	virtual FName GetCategoryName() const override { return FName("Polarity"); }

	UPROPERTY(EditAnywhere, config, Category = "Run Level")
	TSoftObjectPtr<URunLevelDefinition> Definition;

	/** Definition, loaded. Null when none is set: experience is still counted, no level gives anything. */
	static const URunLevelDefinition* GetDefinition();
};
