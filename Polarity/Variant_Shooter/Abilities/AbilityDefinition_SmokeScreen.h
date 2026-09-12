// AbilityDefinition_SmokeScreen.h
// The Melee class's active: a canister that splits into three and walls off a line of sight.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition_Burst.h"
#include "AbilityDefinition_SmokeScreen.generated.h"

class ASmokeCloud;
class ASmokeCanisterProjectile;

/**
 * Bangalore's Smoke Launcher: cover to close the distance with, rather than damage.
 *
 * Shaped after the real thing, which the Apex wiki describes as "fire a high-velocity smoke canister
 * that explodes into a smoke wall on impact" and, in its notes, "upon landing, the canister splits
 * into three, which land in a line perpendicular to where it was launched from. Takes 11 seconds to
 * evaporate." The defaults below are those numbers.
 *
 * It replaces the shield loan, whose payoff only existed if the swing landed and which therefore
 * asked the player to commit before it gave them anything. Smoke gives the approach itself, which is
 * what a melee class is short of.
 *
 * Built on the burst archetype, exactly like the Wizard's puddle, so the montages, the spawn socket,
 * the timing and the audio are all inherited and only the throw itself is new. Author the Levels
 * array from the burst base as usual, with NumProjectiles set to one.
 */
UCLASS(BlueprintType)
class POLARITY_API UAbilityDefinition_SmokeScreen : public UAbilityDefinition_Burst
{
	GENERATED_BODY()

public:
	/** The canister to throw. Harmless; it carries the clouds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	TSubclassOf<ASmokeCanisterProjectile> CanisterProjectileClass;

	/** What each submunition leaves behind. A Blueprint child of ASmokeCloud in practice, so the
	 *  Niagara system and the sound live where they can be seen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	TSubclassOf<ASmokeCloud> CloudClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke", meta = (ClampMin = "100.0", Units = "cm/s"))
	float ProjectileSpeed = 3000.0f;

	/** How hard the canister falls. The arc is what tells the player it lands short of their aim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float ProjectileGravityScale = 0.5f;

	// ==================== The wall ====================

	/** How many pieces the canister breaks into on landing. Three is Bangalore's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Wall", meta = (ClampMin = "1", ClampMax = "9"))
	int32 SplitCount = 3;

	/** How far apart the pieces land, measured along the line across the shot. Kept under twice the
	 *  cloud radius or the wall has holes in it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Wall", meta = (ClampMin = "0.0", Units = "cm"))
	float SplitSpacing = 340.0f;

	/** Radius of one cloud: how far from its own centre it blinds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Wall", meta = (ClampMin = "20.0", Units = "cm"))
	float CloudRadius = 350.0f;

	/** How long a cloud takes to fill out. Blindness and picture ramp in together over this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Wall", meta = (ClampMin = "0.0", Units = "s"))
	float GrowTime = 0.6f;

	/** How long a cloud blocks sight once it lands, growth included. Apex's smoke evaporates in 11
	 *  seconds, and this is that number: the clouds are destroyed on it, so the ability lasts
	 *  exactly as long as it says. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Wall", meta = (ClampMin = "0.5", Units = "s"))
	float Duration = 11.0f;

	/** The tail of that life spent visibly thinning out. Inside Duration, not added to it: the smoke
	 *  goes on blinding while it thins, and stops when it is gone.
	 *
	 *  Deliberately a small slice of the eleven seconds. The cloud is meant to stand at full strength
	 *  and then go, not to be visibly dying from the moment it lands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Wall", meta = (ClampMin = "0.0", Units = "s"))
	float FadeTime = 1.5f;

	// ==================== What it does to the AI ====================

	/** How much smoke an enemy sees THROUGH before it loses you, measured along its line of sight.
	 *
	 *  This one number is the whole feel of the ability. Below it the enemy still sees you, which is
	 *  what makes standing inside the cloud next to somebody useless as a hiding place and what
	 *  keeps a melee class honest: the smoke covers the approach, not the fight at the end of it.
	 *
	 *  TEST VALUE. Roughly half a cloud's width, so clipping the edge of the wall is still seen and
	 *  crossing its middle is not. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|AI", meta = (ClampMin = "0.0", Units = "cm"))
	float SightPenetration = 350.0f;

	/** What an enemy's turn rate is multiplied by while it stands inside a cloud. Restored the
	 *  moment it steps out.
	 *
	 *  Ground NPCs only: flying drones, kamikazes and the turret drive their own rotation and are
	 *  not touched by this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|AI", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float TurnRateMultiplier = 0.4f;
};
