// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "Sound/SoundBase.h"
#include "NiagaraSystem.h"
#include "UpgradeDefinition_AirKick.generated.h"

/**
 * Per-level explosion tuning for Air Kick.
 * Level 1 (index 0) typically has bExplodeOnImpact = false — kick just launches the prop.
 * Level 2 (index 1) flips it on with a fixed damage and radius.
 */
USTRUCT(BlueprintType)
struct FAirKickLevelData
{
	GENERATED_BODY()

	/**
	 * If true, the launched prop is primed to detonate in a radius when it hits an NPC.
	 * If false, the prop instead applies single-target ImpactDamage and bounces.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|Level")
	bool bExplodeOnImpact = false;

	/**
	 * Damage dealt to a single NPC when the launched prop strikes them WITHOUT exploding.
	 * Used when bExplodeOnImpact is false. Routed through the EMFPhysicsProp "weak impact"
	 * path so no explosion VFX/radius — the prop just dings the target and bounces off,
	 * potentially hitting more NPCs after the bounce.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|Level", meta = (ClampMin = "0.0", EditCondition = "!bExplodeOnImpact"))
	float ImpactDamage = 25.0f;

	/** Flat damage dealt by the on-impact explosion (only used when bExplodeOnImpact is true). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|Level", meta = (ClampMin = "0.0", EditCondition = "bExplodeOnImpact"))
	float FixedExplosionDamage = 100.0f;

	/** Explosion radius in cm (only used when bExplodeOnImpact is true). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|Level", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bExplodeOnImpact"))
	float FixedExplosionRadius = 300.0f;
};

/**
 * Data asset for the "Air Kick" upgrade.
 * When the player hits an airborne EMFPhysicsProp with melee while also airborne,
 * the prop is launched forward in the camera direction (same as the previous
 * reverse-channeling kick). The kick also marks the prop so that any subsequent
 * collision with an NPC during flight detonates it with a fixed damage value
 * defined here — so the kick's payload is predictable regardless of the prop's
 * own ExplosionDamage / bCanExplode / charge-scaling configuration.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_AirKick : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	// ==================== Launch Tuning ====================

	/** Speed at which the kicked prop is launched (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick", meta = (ClampMin = "500.0"))
	float LaunchSpeed = 3000.0f;

	/** Maximum distance below the prop to trace for ground (cm).
	 *  If ground is found within this distance, the prop is considered grounded and the kick won't trigger. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick", meta = (ClampMin = "10.0"))
	float PropAirborneTraceDistance = 80.0f;

	/** Spin speed applied to the prop on kick (degrees/second). Zero = no spin. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick", meta = (ClampMin = "0.0"))
	float KickSpinSpeed = 720.0f;

	// ==================== Per-Level Explosion ====================

	/**
	 * Per-level explosion tuning. Index 0 = level 1, index 1 = level 2, etc.
	 * MaxLevel on the parent definition should match the number of entries.
	 * If CurrentLevel exceeds the array, the last entry is reused.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|Levels")
	TArray<FAirKickLevelData> LevelData;

	/** Returns the tuning block for the given level (1-based), safely clamped. */
	const FAirKickLevelData& GetLevelData(int32 Level) const
	{
		if (LevelData.Num() == 0)
		{
			static const FAirKickLevelData Default;
			return Default;
		}
		const int32 Index = FMath::Clamp(Level - 1, 0, LevelData.Num() - 1);
		return LevelData[Index];
	}

	// ==================== VFX ====================

	/** One-shot Niagara effect spawned at kick point */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|VFX")
	TObjectPtr<UNiagaraSystem> KickImpactFX = nullptr;

	/** Scale for the impact VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|VFX", meta = (ClampMin = "0.1"))
	float KickImpactFXScale = 1.0f;

	// ==================== Sound ====================

	/** Sound played at kick impact point */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|Sound")
	TObjectPtr<USoundBase> KickSound = nullptr;

	/** Volume multiplier for kick sound */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|Sound", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float KickSoundVolume = 1.0f;

	/** Pitch multiplier for kick sound */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Kick|Sound", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float KickSoundPitch = 1.0f;
};
