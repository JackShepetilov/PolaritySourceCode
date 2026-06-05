// SkillTypes.h
// Shared enums and structs for the XP / upgrade system.
//
// NOTE: the XP track is now single-pool (one level curve, one counter). ESkillCategory no longer
// drives XP tracking or upgrade-pool filtering — it survives ONLY as a cosmetic tag on
// UUpgradeDefinition (card colour / icon). FSkillState/FSkillCurve are single-instance now.

#pragma once

#include "CoreMinimal.h"
#include "SkillTypes.generated.h"

/** Cosmetic upgrade categories — used only to colour/icon upgrade cards. No longer drives XP. */
UENUM(BlueprintType)
enum class ESkillCategory : uint8
{
	Movement    UMETA(DisplayName = "Movement"),
	Melee       UMETA(DisplayName = "Melee"),
	EMF         UMETA(DisplayName = "Electrokinesis"),
	Weapon      UMETA(DisplayName = "Weapon Handling"),
};

/** The run's single leveling state. Lives in XPSubsystem as one instance. */
USTRUCT(BlueprintType)
struct FSkillState
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Skill")
	int32 CurrentXP = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Skill")
	int32 CurrentLevel = 0;
};

/** The run's single leveling curve and base kill XP. Lives in XPConfig.LevelCurve. */
USTRUCT(BlueprintType)
struct FSkillCurve
{
	GENERATED_BODY()

	/** Cumulative XP needed to reach each level. Length defines max level for this skill. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skill")
	TArray<int32> LevelThresholds;

	/** Base XP awarded for a kill routed to this skill (multiplied by EnemyXPMultiplier). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skill", meta = (ClampMin = "0"))
	int32 BaseXPPerKill = 50;
};

