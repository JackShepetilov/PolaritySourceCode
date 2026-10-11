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
#include "Variant_Shooter/MeleeAttackComponent.h"
#include "UpgradeDefinition_MeleeKit.generated.h"

class UMaterialInterface;

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
 * A kick that lands knocks the player back exactly as it knocks the enemy back: the same shove,
 * the other way. No numbers of its own, on purpose: whatever the kick does to the enemy is what it
 * does to the player.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_KickPush : public UUpgradeDefinition
{
	GENERATED_BODY()

public:
	/** How much of the enemy's shove comes back onto the player. 1 = the same. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kick Push", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float SelfKnockbackScale = 1.0f;
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
 * Hold the melee key for HoldTime and a hold attack comes out by itself; if it lands, the weapon
 * stays away and the player is cloaked, after the Spy in TF2. The cloak ends on equipping a weapon,
 * on swinging again, or on its timer. The swing made from inside the cloak shoves and does no
 * damage.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_MeleeCloak : public UUpgradeDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Cloak", meta = (ClampMin = "0.5", Units = "s"))
	float CloakDuration = 6.0f;

	/** How long the melee key has to stay down for the hold attack to fire. Let go sooner and the
	 *  ordinary swing comes out instead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Cloak|Hold Attack", meta = (ClampMin = "0.1", ClampMax = "2.0", Units = "s"))
	float HoldTime = 0.35f;

	/** The hold attack's own animation, in place of the ordinary swing's. Montage left empty = the
	 *  ordinary swing is played. Weight is not used. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Cloak|Hold Attack")
	FMeleeAnimationData HoldAttackAnimation;

	/** Worn by the melee mesh from the moment the hold attack lands to the end of the swing, so the
	 *  player sees themselves going. A see-through material. Unset = the mesh is left alone. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Cloak|Feedback")
	TObjectPtr<UMaterialInterface> CloakMeleeMaterial;
};
