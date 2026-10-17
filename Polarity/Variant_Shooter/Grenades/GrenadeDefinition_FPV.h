// GrenadeDefinition_FPV.h
// The FPV drone "grenade": thrown out of the hand, it flies off on its own and crashes into the enemy.

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Grenades/GrenadeDefinition.h"
#include "GrenadeDefinition_FPV.generated.h"

class AKamikazeDroneNPC;
class AShooterCharacter;

/**
 * Not a projectile: what leaves the hand is the project's own kamikaze drone, on the thrower's side
 * [author, 2026-10-15]. It is launched the way the carrier launches its munitions
 * (AKamikazeDroneNPC::LaunchAsHomingMunition): off along the throw, then it picks the nearest enemy of
 * its side by itself, switches when that one is gone, and goes off on contact.
 *
 * The blast is the thrower's (credit, and past the enemies' own-side filter), see
 * AKamikazeDroneNPC::DoExplosion. Enemies may shoot it down on the way.
 *
 * ProjectileClass, the fuse and the danger radius of the base are not used.
 */
UCLASS(BlueprintType)
class POLARITY_API UGrenadeDefinition_FPV : public UGrenadeDefinition
{
	GENERATED_BODY()

public:

	UGrenadeDefinition_FPV();

	/** The drone thrown. A Blueprint child of AKamikazeDroneNPC: its own strike, damage and look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FPV")
	TSubclassOf<AKamikazeDroneNPC> DroneClass;

	/** Speed it leaves the hand at, along the aim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FPV", meta = (ClampMin = "100.0", Units = "cm/s"))
	float LaunchSpeed = 1600.0f;

	/** A drone flies: the aim line is drawn straight. */
	virtual float GetThrowSpeed() const override { return LaunchSpeed; }
	virtual float GetThrowGravityScale() const override { return 0.0f; }

	/** Authority: put the drone in the air at Location, facing and flying along Rotation, on Thrower's
	 *  side. Returns it, or null if it could not be made. */
	AKamikazeDroneNPC* LaunchDrone(AShooterCharacter* Thrower, const FVector& Location, const FRotator& Rotation) const;
};
