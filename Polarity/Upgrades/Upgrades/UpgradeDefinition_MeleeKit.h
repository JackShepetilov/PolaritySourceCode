// Copyright 2025 Suspended Caterpillar. All Rights Reserved.
//
// Data for the four melee upgrades of 2026-10-06 (Docs/Abilities_Melee_Design_2026-10-06.md §3).
// One header for all four: each is a handful of numbers and they are read together.
//
// The rule they follow: a melee upgrade either changes the KICK (the swing made in the air) or adds
// an attack on the HELD key. Every number here is a TEST VALUE.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_MeleeKit.generated.h"

// ==================== Momentum ====================

/**
 * Damage for speed toward the target, as a multiplier on the base hit. This used to be a flat bonus
 * built into every swing; it is an upgrade now, and worth up to MaxMultiplier.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_MeleeMomentum : public UUpgradeDefinition
{
	GENERATED_BODY()

public:
	/** Multiplier at FullSpeed and above. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Momentum", meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float MaxMultiplier = 2.25f;

	/** Speed toward the target below which there is no bonus at all. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Momentum", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinSpeed = 700.0f;

	/** Speed toward the target at which the bonus is full. Scales linearly from MinSpeed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Momentum", meta = (ClampMin = "100.0", Units = "cm/s"))
	float FullSpeed = 2200.0f;
};

// ==================== Kick push ====================

/**
 * A kick that lands shoves the enemy and the player apart along the line of the hit.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_KickPush : public UUpgradeDefinition
{
	GENERATED_BODY()

public:
	/** Multiplier on how far the kicked enemy is thrown. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kick Push", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float EnemyKnockbackMultiplier = 2.5f;

	/** Speed the player leaves at, back along the line of the hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kick Push", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SelfPushSpeed = 1100.0f;

	/** Upward speed added to the player's push, so it carries instead of scraping the floor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kick Push", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SelfPushLift = 350.0f;
};

// ==================== Disarm ====================

/**
 * A melee hit leaves the enemy unable to shoot for a while. Meant to stack with smoke: the enemy
 * that cannot see you now cannot answer either.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_MeleeDisarm : public UUpgradeDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Disarm", meta = (ClampMin = "0.1", Units = "s"))
	float DisarmDuration = 3.0f;
};

// ==================== Cloak ====================

/**
 * A swing made with the melee key HELD that lands puts the weapon away and cloaks the player,
 * after the Spy in TF2. The cloak ends on drawing a weapon, on swinging again, or on its timer.
 * The swing made from inside the cloak shoves and does no damage.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_MeleeCloak : public UUpgradeDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Cloak", meta = (ClampMin = "0.5", Units = "s"))
	float CloakDuration = 6.0f;
};
