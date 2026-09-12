// SmokeCanisterProjectile.h
// The Melee class's active, in flight: a thrown canister that splits into three on landing.

#pragma once

#include "CoreMinimal.h"
#include "ShooterProjectile.h"
#include "Variant_Shooter/Abilities/SmokeCloud.h"
#include "SmokeCanisterProjectile.generated.h"

/**
 * Bangalore's smoke launcher, in the shape the wiki describes it: "upon landing, the canister splits
 * into three, which land in a line perpendicular to where it was launched from".
 *
 * So this actor is used twice, and which of the two it is decides what landing means:
 *  - the CANISTER the player throws splits into submunitions and dies;
 *  - a SUBMUNITION drops one cloud and dies.
 * One class rather than two because the flight, the floor search and the ignore rules are the same
 * for both, and the difference is one branch.
 *
 * It deals nothing on contact. Everything the ability does is done by the clouds it leaves.
 *
 * Server side: it is spawned by a handler, and handlers only ever run on the authority.
 */
UCLASS()
class POLARITY_API ASmokeCanisterProjectile : public AShooterProjectile
{
	GENERATED_BODY()

public:
	ASmokeCanisterProjectile();

	/** Send the thrown canister off. */
	void Launch(float Speed, float GravityScale, TSubclassOf<ASmokeCloud> InCloudClass,
		const FSmokeCloudShape& InShape, float InSightPenetration, float InTurnRateMultiplier,
		int32 InSplitCount, float InSplitSpacing);

	/** Put a whole wall down at a point that has already been reached.
	 *
	 *  Static, and that is the point of it: the thrown canister is not the only thing that arrives
	 *  somewhere and wants a wall there. The Melee's charged prop lands too, and it has no flight of
	 *  its own to split out of -- it IS the landing. One implementation, two callers, so a change to
	 *  how the wall forms cannot apply to only half of the class.
	 *
	 *  Spawns SplitCount submunitions of CanisterClass in a fan around ImpactPoint; each drops one
	 *  cloud where it comes to rest. The flight numbers (the hop, the spawn height, the deadline)
	 *  come off CanisterClass's defaults, so they stay authored in one place, on the Blueprint. */
	static void DeploySmokeWall(UWorld* World, TSubclassOf<ASmokeCanisterProjectile> CanisterClass,
		AActor* InOwner, APawn* InInstigator, const FVector& ImpactPoint, const FVector& TravelDirection,
		const FSmokeCloudShape& InShape, TSubclassOf<ASmokeCloud> InCloudClass,
		float InSightPenetration, float InTurnRateMultiplier, int32 InSplitCount, float InSplitSpacing);

	/** How far below the impact point a cloud looks for a floor to sit on. A canister that stops
	 *  against a wall should still smoke the ground under that wall, which is what a player reads
	 *  from "it drops where it lands". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke", meta = (ClampMin = "0.0", Units = "cm"))
	float FloorSearchDistance = 1200.0f;

	/** How high above that floor the middle of a cloud sits. Smoke is a volume and a player is about
	 *  180 tall, so a cloud centred on the floor would hide knees and nothing else. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke", meta = (ClampMin = "0.0", Units = "cm"))
	float CloudHeightAboveFloor = 150.0f;

	// ==================== The split ====================

	/** Upward speed given to each submunition. With gravity this decides how long they are in the
	 *  air, and the sideways speed is then computed from it so that they land on their spacing --
	 *  the line is a result, not a guess. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Split", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SplitUpSpeed = 420.0f;

	/** How high above the landing point the submunitions are born, so they do not spawn inside the
	 *  floor they just hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Split", meta = (ClampMin = "0.0", Units = "cm"))
	float SplitLaunchHeight = 70.0f;

	/** A submunition that never lands -- thrown off a ledge, out over water -- deploys anyway after
	 *  this long. Without it the ability can silently do nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Split", meta = (ClampMin = "0.1", Units = "s"))
	float SplitMaxFlightTime = 1.6f;

protected:
	virtual void ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& HitLocation,
		const FVector& HitDirection) override;

	/** Throw the submunitions out along the line across the shot. */
	void SplitInto(const FVector& ImpactPoint, const FVector& HitDirection);

	/** Drop this submunition's cloud and stop. Safe to call twice; the second call does nothing. */
	void Deploy(const FVector& ImpactPoint);

	UFUNCTION()
	void DeployWhereverIAm();

	/** Where a cloud should actually sit, given where a canister stopped. */
	FVector FindFloorUnder(const FVector& ImpactPoint) const;

	UPROPERTY()
	TSubclassOf<ASmokeCloud> CloudClass;

	FSmokeCloudShape Shape;
	float SightPenetration = 350.0f;
	float TurnRateMultiplier = 0.4f;

	int32 SplitCount = 3;
	float SplitSpacing = 300.0f;

	/** False on the canister the player threw, true on the three it becomes. */
	bool bSubmunition = false;

	bool bDeployed = false;

	FTimerHandle DeployTimer;

	/** Turn this freshly spawned canister into one piece of a wall and send it off. */
	void BecomeSubmunition(TSubclassOf<ASmokeCloud> InCloudClass, const FSmokeCloudShape& InShape,
		float InSightPenetration, float InTurnRateMultiplier, const FVector& Velocity);
};
