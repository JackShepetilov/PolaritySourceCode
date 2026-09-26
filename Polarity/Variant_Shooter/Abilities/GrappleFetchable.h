// GrappleFetchable.h
// Anything the grapple can hook and reel in instead of swinging on.
//
// The fetch used to know one thing only, ADroppedRangedWeapon, and that type was written into the
// hook, the character's claim and the reticle. The dispenser slot machine hands its upgrades out as
// physical items the player takes with the hook the same way (the author's call, 2026-09-25), so the
// fetch now asks this interface and nothing else. A dropped weapon behaves exactly as before.
//
// The hook finds candidates through GrappleFetch::GetAll, a per-world list the fetchables register
// themselves in: the owning client runs the search every frame, and a walk over every actor in the
// world would be the wrong price for "a handful of things lying around".

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GrappleFetchable.generated.h"

class AActor;
class AShooterCharacter;
class UWorld;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UGrappleFetchable : public UInterface
{
	GENERATED_BODY()
};

class POLARITY_API IGrappleFetchable
{
	GENERATED_BODY()

public:

	/** Free for Caster to hook right now, distance and aim aside: meant to be picked up, not already
	 *  flying to anybody, not already taken, and Caster is allowed to (a slot machine box belongs to
	 *  whoever spun). Asked on the owning client for the brackets and on the server for the claim. */
	virtual bool CanBeGrappleFetchedBy(const AShooterCharacter* Caster) const = 0;

	/** The hook has reached it: start flying to Caster's line hand. Server. False when refused
	 *  (somebody else got there first); the caller then gives the stowed weapon back. On arrival the
	 *  item itself ends the fetch with AShooterCharacter::FinishWeaponFetch. */
	virtual bool BeginGrappleFetchPull(AShooterCharacter* Caster) = 0;

	/** Flying to somebody right now: the line's far end rides on it. */
	virtual bool IsGrappleFetchInFlight() const = 0;

	/** Taken, or no longer there to take: the line stops following it. */
	virtual bool IsGrappleFetchDone() const = 0;

	/** True when the item gives the caster's hands back itself on arrival (FinishWeaponFetch). The
	 *  rest are watched by the caster, who draws again once IsGrappleFetchDone. */
	virtual bool FinishesFetchItself() const { return false; }
};

namespace GrappleFetch
{
	/** Put Actor on its world's list. Call from BeginPlay on every machine. */
	POLARITY_API void Register(AActor* Actor);

	/** Take Actor off its world's list. Call from EndPlay. */
	POLARITY_API void Unregister(AActor* Actor);

	/** Every registered fetchable of World that still exists. */
	POLARITY_API void GetAll(const UWorld* World, TArray<AActor*>& OutActors);

	/** Who answers the fetch for Actor: a component implementing IGrappleFetchable first (a
	 *  dispenser box takes over the item lying in it), else the actor itself, else null. */
	POLARITY_API IGrappleFetchable* Resolve(AActor* Actor);
	POLARITY_API const IGrappleFetchable* Resolve(const AActor* Actor);
}
