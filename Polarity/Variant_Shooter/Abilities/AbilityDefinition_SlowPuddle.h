// AbilityDefinition_SlowPuddle.h
// The Wizard's active: a straight bolt that leaves a puddle, and the puddle slows enemies in it.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition_Burst.h"
#include "AbilityDefinition_SlowPuddle.generated.h"

class ASlowPuddle;
class ASlowPuddleProjectile;

/**
 * Catalyst's tactical, with the damage left out on purpose: the puddle is control and nothing else.
 * The Wizard's passive already multiplies what the team's fire is worth, so a puddle that also hurt
 * would read as more damage rather than as a place enemies do not want to be.
 *
 * Built on the burst archetype, exactly like the shield-bypass active it replaces, so the montages,
 * the spawn socket, the timing and the audio are all inherited and only the shot itself is new.
 * Author the Levels array from the burst base as usual, with NumProjectiles set to one.
 */
UCLASS(BlueprintType)
class POLARITY_API UAbilityDefinition_SlowPuddle : public UAbilityDefinition_Burst
{
	GENERATED_BODY()

public:
	/** The bolt to throw. Straight, harmless, and carries the puddle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle")
	TSubclassOf<ASlowPuddleProjectile> PuddleProjectileClass;

	/** What it leaves behind. A Blueprint child of ASlowPuddle in practice, so the Niagara system
	 *  and the sound live where they can be seen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle")
	TSubclassOf<ASlowPuddle> PuddleClass;

	/** Flight speed of the bolt. Kept from the ability it replaces, so the throw still feels the
	 *  same in the hand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle", meta = (ClampMin = "100.0", Units = "cm/s"))
	float ProjectileSpeed = 3000.0f;

	/** How wide the puddle lies on the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle", meta = (ClampMin = "20.0", Units = "cm"))
	float PuddleRadius = 250.0f;

	/** How long it stays before drying up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle", meta = (ClampMin = "0.5", Units = "s"))
	float PuddleDuration = 8.0f;

	/** What an enemy's movement speed is multiplied by while it stands in the puddle. Restored the
	 *  moment it steps out.
	 *
	 *  TEST VALUE. Half speed is the shield-bypass slow this ability inherits its numbers from; the
	 *  right value for a puddle a whole squad can walk into is a balance decision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MoveSpeedMultiplier = 0.5f;
};
