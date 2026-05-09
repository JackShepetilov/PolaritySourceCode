// SkillTypes.h
// Shared enums and structs for the per-skill XP system.
// Add or remove entries in ESkillCategory / EMovementAction to expand or shrink the system —
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

/** Movement actions tracked for Movement XP (used by MovementXPTracker — Stage Б). */
UENUM(BlueprintType)
enum class EMovementAction : uint8
{
	AirDash     UMETA(DisplayName = "Air Dash"),
	DoubleJump  UMETA(DisplayName = "Double Jump"),
	Slide       UMETA(DisplayName = "Slide"),
	WallRun     UMETA(DisplayName = "Wall Run"),
	Mantle      UMETA(DisplayName = "Mantle"),
	WallBounce  UMETA(DisplayName = "Wall Bounce"),
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

/**
 * Per-action movement parameters. Discrete actions award XPPerEvent on each trigger.
 * Continuous actions use an active/cooldown cycle: while active for ActiveGainSeconds the
 * tracker awards XPPerSecond * Weight per second; then awards stop until CooldownSeconds elapse.
 */
USTRUCT(BlueprintType)
struct FMovementActionConfig
{
	GENERATED_BODY()

	/** Discrete = single event awards XPPerEvent. Continuous = active/cooldown cycle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement Action")
	bool bDiscrete = false;

	/** XP multiplier for this action. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement Action", meta = (ClampMin = "0.0"))
	float Weight = 1.f;

	/** Seconds of active XP gain (continuous only). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement Action", meta = (ClampMin = "0.0"))
	float ActiveGainSeconds = 5.f;

	/** Seconds of cooldown after active window closes (continuous only). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement Action", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 5.f;

	/** XP per second during active window (continuous only). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement Action", meta = (ClampMin = "0"))
	int32 XPPerSecond = 10;

	/** XP for one event (discrete only). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement Action", meta = (ClampMin = "0"))
	int32 XPPerEvent = 25;
};
