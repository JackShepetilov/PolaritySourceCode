// SmokeVisionSubsystem.h
// Who is hidden by smoke, answered by geometry rather than by collision.
//
// The smoke has no collision component of any kind, and that is the whole design: a cloud that
// blocked a trace channel would also have to be kept out of the way of bullets, the camera, cover
// scoring and every other trace in the project. Instead the clouds register here, and anything that
// asks "can A see B" measures the segment against their spheres.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SmokeVisionSubsystem.generated.h"

class ASmokeCloud;
class APawn;

UCLASS()
class POLARITY_API USmokeVisionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	/** Clouds announce themselves for their whole life, on every machine. Registering on clients too
	 *  costs nothing and keeps the pair symmetric: BeginPlay adds, EndPlay removes, and there is no
	 *  authority branch to get wrong. */
	void RegisterCloud(ASmokeCloud* Cloud);
	void UnregisterCloud(ASmokeCloud* Cloud);

	bool HasAnySmoke() const { return Clouds.Num() > 0; }

	/** Total length of the segment From->To that runs inside smoke, in centimetres. Zero when there
	 *  is no smoke at all, which is the common case and costs one emptiness check.
	 *
	 *  Overlapping puffs are counted twice on purpose: a wall built from overlapping spheres should
	 *  read as thicker than a single puff. Tune SightPenetration, not this. */
	float GetSightBlockage(const FVector& From, const FVector& To) const;

	/** The same question as a yes/no, against the penetration the active smoke was authored with. */
	bool IsSightBlocked(const FVector& From, const FVector& To) const;

	/** Is this point standing inside a cloud that THIS pawn threw?
	 *
	 *  Asked by UApexMovementComponent for the Melee's charged smoke jump, so it is deliberately a
	 *  question the movement asks rather than something a cloud tells the character: an overlap
	 *  event fires outside the movement simulation and its velocity would be replayed away on the
	 *  owning client (Docs/Gotchas/Movement_Network.md). Same shape as UNitroGateSubsystem.
	 *
	 *  The FULL radius, not GetCurrentRadius(): the growth ramp is measured from each machine's own
	 *  spawn time, so the current radius is two slightly different numbers on the two ends and this
	 *  answer has to be one. The half second of ramp it ignores is the half second the cloud is too
	 *  small to stand in anyway. */
	bool IsInsideSmokeFrom(const FVector& Point, const APawn* Thrower) const;

	/** How much smoke an observer sees through before it stops seeing. Written by a cloud when it
	 *  starts, so the number lives with the ability's level data rather than being duplicated here.
	 *  Two clouds of different levels at once means the last one to land wins, which is a rounding
	 *  error next to what it would cost to make the threshold per-cloud: the blockage is a sum
	 *  across clouds and cannot be attributed back to one of them. */
	void SetSightPenetration(float Centimetres);

	float GetSightPenetration() const { return SightPenetration; }

	/** Convenience for the call sites that have a world and nothing else. Null-safe: no world or no
	 *  subsystem means no smoke, never a blocked line. */
	static bool IsSightBlockedInWorld(const UWorld* World, const FVector& From, const FVector& To);

	/** How often the clouds re-measure who is standing in them. */
	UPROPERTY(EditAnywhere, Category = "Smoke", meta = (ClampMin = "0.05", Units = "s"))
	float OccupantRefreshInterval = 0.25f;

protected:

	/** Find every NPC once and hand the list to every live cloud.
	 *
	 *  This lives here rather than on the cloud because a wall is THREE clouds: three actors each
	 *  looking for enemies on their own timer is the same search done three times, and the search is
	 *  the expensive half. One sweep, then a distance test per cloud per enemy, which is free. */
	void RefreshAllOccupants();

	/** Start the sweep when the first cloud appears, stop it when the last one goes. Nothing runs
	 *  while there is no smoke in the world, which is almost always. */
	void UpdateSweepTimer();

	FTimerHandle SweepTimer;

	/** Length of the part of segment A->B that lies inside the sphere (Centre, Radius). */
	static float SegmentInsideSphere(const FVector& A, const FVector& B, const FVector& Centre, float Radius);

	UPROPERTY()
	TArray<TWeakObjectPtr<ASmokeCloud>> Clouds;

	float SightPenetration = 350.0f;
};
