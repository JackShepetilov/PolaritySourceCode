// HealGrenadeProjectile.h
// The heal grenade in flight: bursts on the first solid thing it meets and leaves a heal there.

#pragma once

#include "CoreMinimal.h"
#include "ShooterProjectile.h"
#include "HealGrenadeProjectile.generated.h"

class AHealZone;

/** What the grenade carries, copied off the ability at the throw. A plain struct: it never travels. */
struct FHealGrenadePayload
{
	TSubclassOf<AHealZone> ZoneClass;
	float MinHeal = 10.0f;
	float MaxHeal = 60.0f;
	float MinDistance = 300.0f;
	float MaxDistance = 2500.0f;
	float ZoneRadius = 150.0f;
	float ZoneLifetime = 10.0f;
};

/**
 * It deals nothing on contact. Everything the ability does is done by the zone it leaves, and how
 * much that zone is worth is decided here, from how far this flew.
 *
 * Replicated, unlike an ordinary round: nothing multicasts an ability's projectile to the clients,
 * and a heal a teammate is supposed to run towards has to be seen in the air by that teammate.
 *
 * Server side: it is spawned by a handler, and handlers only ever run on the authority.
 */
UCLASS()
class POLARITY_API AHealGrenadeProjectile : public AShooterProjectile
{
	GENERATED_BODY()

public:
	AHealGrenadeProjectile();

	/** Send it off, carrying the heal it will leave behind. */
	void Launch(float Speed, float GravityScale, const FHealGrenadePayload& InPayload);

	/** How far below the burst the zone looks for a floor to settle on. Short on purpose: a grenade
	 *  that bursts on a teammate's chest should heal at their feet, but one that bursts high on a
	 *  wall stays up there, where only somebody moving well can reach it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Grenade", meta = (ClampMin = "0.0", Units = "cm"))
	float FloorSearchDistance = 250.0f;

	/** How high above that floor the middle of the zone sits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal Grenade", meta = (ClampMin = "0.0", Units = "cm"))
	float ZoneHeightAboveFloor = 90.0f;

protected:
	virtual void ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& HitLocation,
		const FVector& HitDirection) override;

	FHealGrenadePayload Payload;

	/** Where it left the hand. The heal is scaled by the straight line from here to the burst. */
	FVector LaunchLocation = FVector::ZeroVector;
};
