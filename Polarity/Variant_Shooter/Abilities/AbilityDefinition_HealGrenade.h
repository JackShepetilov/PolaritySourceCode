// AbilityDefinition_HealGrenade.h
// A thrown grenade that bursts on the first solid thing it meets and leaves a heal there.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition_Throw.h"
#include "AbilityDefinition_HealGrenade.generated.h"

class AHealGrenadeProjectile;
class AHealZone;

/**
 * The heal is worth more the farther the grenade travelled, after the Medic's crossbow in TF2. That
 * one rule gives it both of its uses: a long accurate throw onto a teammate, or a throw ahead of
 * yourself that you then have to cover the ground to collect.
 *
 * Distance is the straight line from where it left the hand to where it burst, not the length of
 * the arc. Measured along the arc, a throw straight up and a step sideways would be a full heal.
 *
 * Every number here is a TEST VALUE.
 */
UCLASS(BlueprintType)
class POLARITY_API UAbilityDefinition_HealGrenade : public UAbilityDefinition_Throw
{
	GENERATED_BODY()

public:
	/** The grenade in flight. Harmless; it carries the heal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Grenade")
	TSubclassOf<AHealGrenadeProjectile> GrenadeClass;

	/** What it leaves behind. A Blueprint child of AHealZone in practice, so the effect and the
	 *  sounds live where they can be seen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Grenade")
	TSubclassOf<AHealZone> ZoneClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Grenade|Flight", meta = (ClampMin = "100.0", Units = "cm/s"))
	float ProjectileSpeed = 2400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Grenade|Flight", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float ProjectileGravityScale = 1.0f;

	// ==================== Heal by distance ====================

	/** Heal at MinDistance and anything nearer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Grenade|Heal", meta = (ClampMin = "0.0"))
	float MinHeal = 10.0f;

	/** Heal at MaxDistance and anything farther. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Grenade|Heal", meta = (ClampMin = "0.0"))
	float MaxHeal = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Grenade|Heal", meta = (ClampMin = "0.0", Units = "cm"))
	float MinDistance = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Grenade|Heal", meta = (ClampMin = "1.0", Units = "cm"))
	float MaxDistance = 2500.0f;

	// ==================== The zone ====================

	/** How close a player has to come to take the heal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Grenade|Zone", meta = (ClampMin = "20.0", Units = "cm"))
	float ZoneRadius = 150.0f;

	/** How long the heal waits to be taken before it is gone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Grenade|Zone", meta = (ClampMin = "0.5", Units = "s"))
	float ZoneLifetime = 10.0f;

	virtual float GetThrowSpeed() const override { return ProjectileSpeed; }
	virtual float GetThrowGravityScale() const override { return ProjectileGravityScale; }
};
