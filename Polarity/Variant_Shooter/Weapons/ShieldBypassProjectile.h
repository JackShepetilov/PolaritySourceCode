// ShieldBypassProjectile.h
// The Wizard's active, in flight: one homing bolt that turns an enemy's shield into a wound.

#pragma once

#include "CoreMinimal.h"
#include "ShooterProjectile.h"
#include "ShieldBypassProjectile.generated.h"

/**
 * Flies at one locked enemy and, on arrival, opens it: for a few seconds the ionization that would
 * have filled its shield goes into its health instead, multiplied. The bolt itself deals nothing.
 *
 * True homing rather than ballistic: the target is chosen when the ability is cast and never
 * re-picked, gravity is off, and the movement component steers hard enough that the bolt does not
 * miss. The point of the ability is the conversion, not the marksmanship, so the flight is a
 * delivery delay and a readable telegraph rather than a skill check.
 *
 * Server side. It is spawned by a handler, and handlers only ever run on the authority.
 */
UCLASS()
class POLARITY_API AShieldBypassProjectile : public AShooterProjectile
{
	GENERATED_BODY()

public:
	AShieldBypassProjectile();

	/** Lock onto Target and start flying. Speed and steering come from the ability that fired it.
	 *  DamageMultiplier scales the enemy's own charge into the health damage delivered. */
	void LaunchAt(AActor* Target, float Speed, float DamageMultiplier, float SlowDuration, float SlowMultiplier);

	/** How far the bolt looks for something to latch onto while flying. A cast with nothing in sight
	 *  is no longer refused: the bolt leaves anyway and finds its own target, which is what makes
	 *  firing round a corner work. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass", meta = (ClampMin = "0.0", Units = "cm"))
	float ScanRadius = 900.0f;

	/** How often it looks, while it has nobody. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass", meta = (ClampMin = "0.02", Units = "s"))
	float ScanInterval = 0.1f;

	/** Played where the bolt opens an enemy. One system, not a positive/negative pair like
	 *  AEMFProjectile has: that pair is chosen by the projectile's own charge sign, and this bolt
	 *  carries no charge, so there is nothing to switch on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bypass|VFX")
	TObjectPtr<class UNiagaraSystem> ImpactVFX;

protected:
	/** ProcessHit runs on the authority alone, so spawning the system there would show it to the host
	 *  and to nobody else. Reliable rather than unreliable on purpose: the bolt is destroyed right
	 *  after the hit, and an unreliable RPC from a dying actor is the one that quietly goes missing. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayImpactVFX(FVector Location);

	virtual void Tick(float DeltaSeconds) override;

	/** Look for the nearest live enemy within ScanRadius and latch onto it. */
	void ScanForTarget();

	float TimeSinceScan = 0.0f;

	virtual void ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& HitLocation, const FVector& HitDirection) override;

	/** Who this was fired at. Held weakly: the enemy can die to somebody else mid-flight, and the
	 *  bolt must not keep it alive or crash chasing it. */
	UPROPERTY()
	TWeakObjectPtr<AActor> LockedTarget;

	/** Multiplier applied to ionization while the window this bolt opens is running. */
	float ConversionMultiplier = 1.0f;

	/** Slow applied on arrival, so the opened enemy stays where the team can use it. */
	float ArrivalSlowDuration = 0.0f;
	float ArrivalSlowMultiplier = 1.0f;
};
