// AbilityDefinition_ShieldBypass.h
// The Wizard's active: one homing bolt that opens an enemy's shield for a few seconds.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition_Burst.h"
#include "AbilityDefinition_ShieldBypass.generated.h"

/**
 * Built on the burst archetype rather than beside it, so the cast plays the same montages, spawns
 * from the same socket and makes the same noise as ChargeBurst. Everything about the presentation is
 * inherited; only what the shot DOES is new here.
 *
 * Author the Levels array from the burst base as usual, with NumProjectiles set to one.
 *
 * What the bolt does on arrival is open a window, not deal damage. During that window the ionization
 * that would have gone into the enemy's shield goes into its health instead, multiplied. That is the
 * point of the ability: it does not kill anything by itself, it changes what the team's existing
 * fire is worth for a few seconds.
 */
UCLASS(BlueprintType)
class POLARITY_API UAbilityDefinition_ShieldBypass : public UAbilityDefinition_Burst
{
	GENERATED_BODY()

public:
	/** The bolt to fire. Its own class, not the burst projectile: it carries the window it opens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass")
	TSubclassOf<class AShieldBypassProjectile> BypassProjectileClass;

	/** Flight speed of the bolt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass", meta = (ClampMin = "100.0", Units = "cm/s"))
	float ProjectileSpeed = 3000.0f;

	/** How long the enemy stays open after the bolt lands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass", meta = (ClampMin = "0.1", Units = "s"))
	float Duration = 4.0f;

	/** Multiplies ionization as it is redirected into health during the window. The beam's own rate
	 *  fills a shield; as a damage rate it is almost nothing, which is exactly how the ability read
	 *  when this was 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass", meta = (ClampMin = "0.0"))
	float RedirectDamageMultiplier = 6.0f;

	/** Movement speed while open, so a stripped enemy stays where the team can use it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MoveSpeedMultiplier = 0.5f;

	/** How far the bolt will look for a target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass", meta = (ClampMin = "100.0", Units = "cm"))
	float TargetSearchRange = 4000.0f;
};
