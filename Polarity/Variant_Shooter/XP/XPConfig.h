// XPConfig.h
// PrimaryDataAsset configuring the per-skill XP economy.
//
// Replaces the old single-curve config: the system is now multi-skill, where each ESkillCategory
// has its own leveling curve, base kill XP, and (via DamageType routing) source events.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SkillTypes.h"
#include "ShooterNPC.h"
#include "XPConfig.generated.h"

class UDamageType;

UCLASS(BlueprintType)
class POLARITY_API UXPConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Per-skill leveling curves and base kill XP. Author entries for each ESkillCategory you ship. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "XP|Skills")
	TMap<ESkillCategory, FSkillCurve> SkillCurves;

	/** Multiplier applied to base kill XP, per enemy class. NPCs not in this map get 1.0x. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "XP|Enemies")
	TMap<TSubclassOf<AShooterNPC>, float> EnemyXPMultiplier;

	/** Routes a kill's DamageType to a skill category. Unmapped types award no kill XP (with warning). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "XP|Routing")
	TMap<TSubclassOf<UDamageType>, ESkillCategory> KillXPRouting;

	/** Per-action parameters for Movement XP (consumed by MovementXPTracker — Stage Б). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "XP|Movement")
	TMap<EMovementAction, FMovementActionConfig> MovementActions;

	// === Helpers ===

	/** Cumulative XP threshold required to reach the given level (>= 1) within Category. INT32_MAX out of range. */
	int32 GetThresholdForLevel(ESkillCategory Category, int32 Level) const;

	int32 GetMaxLevel(ESkillCategory Category) const;

	int32 GetBaseXPPerKill(ESkillCategory Category) const;

	/** Returns multiplier for enemy class. 1.0 if class not in table. */
	float GetEnemyMultiplier(TSubclassOf<AShooterNPC> EnemyClass) const;

	/** Resolves DamageType to skill category. Returns false if unmapped. */
	bool GetSkillForDamageType(TSubclassOf<UDamageType> DamageType, ESkillCategory& OutCategory) const;

	/** Returns nullptr if no entry for this action. */
	const FMovementActionConfig* GetMovementConfig(EMovementAction Action) const;
};
