// SkillTypes.h
// Shared enums and structs for the per-skill XP system.
// Add or remove entries in ESkillCategory to expand or shrink the system —
// no other code change is required outside config asset re-tuning.

#pragma once

#include "CoreMinimal.h"
#include "SkillTypes.generated.h"

/** Skill categories tracked independently. Stored as TMap key in XPSubsystem and XPConfig. */
UENUM(BlueprintType)
enum class ESkillCategory : uint8
{
	Movement    UMETA(DisplayName = "Movement"),
	Melee       UMETA(DisplayName = "Melee"),
	EMF         UMETA(DisplayName = "Electrokinesis"),
	Weapon      UMETA(DisplayName = "Weapon Handling"),
};

/** Per-skill leveling state. Lives in XPSubsystem, keyed by ESkillCategory. */
USTRUCT(BlueprintType)
struct FSkillState
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Skill")
	int32 CurrentXP = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Skill")
	int32 CurrentLevel = 0;
};

/** Per-skill leveling curve and base kill XP. Lives in XPConfig, keyed by ESkillCategory. */
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

