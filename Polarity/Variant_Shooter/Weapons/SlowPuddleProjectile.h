// SlowPuddleProjectile.h
// The Wizard's active, in flight: a straight shot that leaves a slowing puddle where it lands.

#pragma once

#include "CoreMinimal.h"
#include "ShooterProjectile.h"
#include "SlowPuddleProjectile.generated.h"

class ASlowPuddle;

/**
 * The same bolt the Wizard already threw, with the homing taken out. It flies where it was aimed and
 * stops at the first thing it meets; what it was aimed AT no longer matters, because the ability now
 * targets a piece of floor rather than an enemy.
 *
 * It deals nothing on contact. Everything the ability does is done by the puddle it leaves.
 *
 * Server side: it is spawned by a handler, and handlers only ever run on the authority.
 */
UCLASS()
class POLARITY_API ASlowPuddleProjectile : public AShooterProjectile
{
	GENERATED_BODY()

public:
	ASlowPuddleProjectile();

	/** Send it off, carrying the puddle it will leave behind. */
	void Launch(float Speed, TSubclassOf<ASlowPuddle> InPuddleClass, float InRadius, float InDuration,
		float InSlowMultiplier);

	/** How far below the impact point the puddle looks for a floor to lie on.
	 *
	 *  It exists because the bolt flies straight and can therefore stop against a wall, and a puddle
	 *  standing upright on a wall is not a puddle. Landing on the floor UNDER the wall hit is the
	 *  reading a player expects from "it drops where it lands". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle", meta = (ClampMin = "0.0", Units = "cm"))
	float FloorSearchDistance = 1200.0f;

protected:
	virtual void ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& HitLocation,
		const FVector& HitDirection) override;

	/** Where the puddle should actually lie, given where the bolt stopped. */
	FVector FindFloorUnder(const FVector& ImpactPoint) const;

	UPROPERTY()
	TSubclassOf<ASlowPuddle> PuddleClass;

	float PuddleRadius = 250.0f;
	float PuddleDuration = 8.0f;
	float PuddleSlowMultiplier = 0.5f;
};
