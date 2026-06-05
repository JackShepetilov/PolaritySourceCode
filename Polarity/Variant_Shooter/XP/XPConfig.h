// XPConfig.h
// PrimaryDataAsset configuring the single-track XP economy for a roguelite run.
//
// XP is one global pool: every player kill awards BaseXPPerKill * EnemyXPMultiplier[class]
// into a single level track (LevelCurve). Upgrades are no longer routed by skill category —
// ESkillCategory survives only as a cosmetic tag on UUpgradeDefinition (card colour/icon).

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
	/** The run's single leveling curve and base kill XP. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "XP")
	FSkillCurve LevelCurve;

	/** Multiplier applied to base kill XP, per enemy class. NPCs not in this map get 1.0x. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "XP|Enemies")
	TMap<TSubclassOf<AShooterNPC>, float> EnemyXPMultiplier;

	/**
	 * DamageTypes whose kills are always attributed to the player, even if the engine's
	 * Causer/Owner/Instigator chain does not lead back to the PlayerPawn.
	 *
	 * Use this for indirect kills where attribution is gameplay-implied but not engine-tracked,
	 * e.g.:
	 *   - DamageType_EMFWeapon kills via thrown/exploded props (Causer = prop, no Instigator)
	 *   - DamageType_Wallslam where the killed NPC slammed into a wall after a player throw
	 *
	 * NOTE: this bypasses player-attribution. Don't add DamageTypes that NPCs use against each
	 * other organically (without player initiation), or you'll award XP for ambient NPC fights.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "XP|Routing")
	TSet<TSubclassOf<UDamageType>> AlwaysAttributeToPlayer;

	/** True if the given DamageType is in AlwaysAttributeToPlayer (skip Causer/Instigator check). */
	bool ShouldAlwaysAttributeToPlayer(TSubclassOf<UDamageType> DamageType) const;


	// === Helpers ===

	/** Cumulative XP threshold required to reach the given level (>= 1). INT32_MAX out of range. */
	int32 GetThresholdForLevel(int32 Level) const;

	int32 GetMaxLevel() const;

	int32 GetBaseXPPerKill() const;

	/** Returns multiplier for enemy class. 1.0 if class not in table. */
	float GetEnemyMultiplier(TSubclassOf<AShooterNPC> EnemyClass) const;
};
