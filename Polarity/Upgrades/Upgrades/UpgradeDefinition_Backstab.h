// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_Backstab.generated.h"

/**
 * Per-level tuning for the Backstab upgrade. Mirror the Combo / AirKick pattern even
 * though Backstab is intended to be single-level — the array makes it trivial to add
 * a level 2 (wider angle / higher multiplier / lower stun requirement) later.
 */
USTRUCT(BlueprintType)
struct FBackstabLevelData
{
	GENERATED_BODY()

	/**
	 * Damage multiplier applied to the melee base damage when both conditions
	 * are met (target is stunned AND player is in the back cone).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Backstab", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float DamageMultiplier = 3.0f;

	/**
	 * Half-angle of the cone behind the target where the player counts as "behind".
	 *   180° = entire back hemisphere (anywhere not directly in front).
	 *    90° = ±90° cone centred on the target's back (~half hemisphere).
	 *    45° = tight ±45° cone directly behind the target.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Backstab", meta = (ClampMin = "10.0", ClampMax = "180.0", Units = "deg"))
	float BackConeHalfAngle = 90.0f;

	/**
	 * If true, only IsStunnedByExplosion() counts — narrow "this NPC was caught in
	 * a blast" path. If false, any IsInKnockback() qualifies (explosion stun OR
	 * regular knockback from melee impact, prop collision, etc.).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Backstab")
	bool bRequireStunnedByExplosion = false;
};

/**
 * Data asset for the "Backstab" upgrade (working title — final game-facing name
 * TBD, leaning toward TF2-Spy references like "Your Eternal Reward" or "Sapper").
 *
 * When the player lands a melee swing on an NPC that's currently stunned AND the
 * player is positioned within the back cone of that NPC, the swing's base damage
 * is multiplied by DamageMultiplier (default 3x). Other damage sources on the
 * same swing (momentum, dropkick) are unaffected.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_Backstab : public UUpgradeDefinition
{
	GENERATED_BODY()

public:

	/** Per-level tuning. Index 0 = level 1. MaxLevel on parent should equal array size. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Backstab|Levels")
	TArray<FBackstabLevelData> LevelData;

	/** Returns the tuning block for the given level (1-based), safely clamped. */
	const FBackstabLevelData& GetLevelData(int32 Level) const
	{
		if (LevelData.Num() == 0)
		{
			static const FBackstabLevelData Default;
			return Default;
		}
		const int32 Index = FMath::Clamp(Level - 1, 0, LevelData.Num() - 1);
		return LevelData[Index];
	}
};
