// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EMFPhysicsProp.h"
#include "BuildingMarkable.h"
#include "TurretBuilding.generated.h"

class AKamikazeDroneNPC;
class ATargetPoint;
class UGeometryCollectionComponent;

/**
 * Destructible skyscraper that holds level-placed turrets.
 *
 * Lifecycle:
 *   1. Player marks the building with the 360 Shot upgrade — IBuildingMarkable::OnMarked fires.
 *   2. We pick the best drone spawn point (ATargetPoint placed in the level) by dot product
 *      between the wall normal at the hit point and the direction to each spawn point —
 *      so the drone always comes in from the side the player is on.
 *   3. A kamikaze drone spawns there in Direct attack mode targeting this building.
 *   4. On drone impact (TakeDamage from drone's crash explosion), Collapse() runs:
 *        - radial damage in CollapseDamageRadius kills nearby actors (turrets, anyone too close)
 *        - the parent EMFPhysicsProp death pipeline spawns the GC and broadcasts OnPropDeath
 *
 * Checkpoint respawn is inherited for free from AEMFPhysicsProp (CheckpointSubsystem
 * registers the prop and calls RestoreFromCheckpointState/ResetProp on player respawn).
 * Turrets self-register as NPCs and respawn independently.
 */
UCLASS()
class POLARITY_API ATurretBuilding : public AEMFPhysicsProp, public IBuildingMarkable
{
	GENERATED_BODY()

public:

	ATurretBuilding();

	// ==================== Drone Targeting ====================

	/** Class of drone to spawn when marked */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Drone")
	TSubclassOf<AKamikazeDroneNPC> DroneClass;

	/** Target points in the level used as drone spawn locations.
	 *  On mark, the point with the highest dot(WallNormal, DirToPoint) is chosen —
	 *  the drone comes in from the side the player is on. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Building|Drone")
	TArray<TObjectPtr<ATargetPoint>> DroneSpawnPoints;

	// ==================== Collapse ====================

	/** Radial damage radius applied at the building's center on collapse — kills turrets and anything too close (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Building|Collapse", meta = (ClampMin = "100.0", Units = "cm"))
	float CollapseDamageRadius = 2000.0f;

	/** Damage value applied by the collapse radial damage. Set high to one-shot turrets. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Building|Collapse", meta = (ClampMin = "1.0"))
	float CollapseDamage = 99999.0f;

	/** Damage type for the collapse radial damage. Falls back to UDamageType if null. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Building|Collapse")
	TSubclassOf<UDamageType> CollapseDamageType;

	/** Total time over which the building progressively collapses top-to-bottom (seconds).
	 *  At t=0 a slice at the very top is unanchored and starts falling under gravity.
	 *  Over CollapseDuration the unanchored slice descends to the foundation, releasing chunks
	 *  layer by layer — gives a 9/11-style pancake collapse instead of a uniform burst. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Building|Collapse|Cascade", meta = (ClampMin = "0.5", ClampMax = "20.0", Units = "s"))
	float CollapseDuration = 3.0f;

	/** Fraction of the building height (from the top) that is released at t=0.
	 *  0.1 = top 10% falls immediately and crushes the rest.
	 *  Tune for the visual moment when the top "lets go" before the cascade. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Building|Collapse|Cascade", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float InitialReleaseHeightFraction = 0.15f;

	/** How often the cascade timer ticks to release the next slice (seconds).
	 *  Smaller = smoother release, slightly more CPU. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Building|Collapse|Cascade", meta = (ClampMin = "0.02", ClampMax = "0.5"))
	float CascadeTickInterval = 0.05f;

	/** Should the foundation (the very bottom slice) eventually be released as well?
	 *  False = foundation stays anchored as rubble (more realistic for a high-rise).
	 *  True = whole structure ends up dynamic. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Building|Collapse|Cascade")
	bool bReleaseFoundation = false;

	/** Local-frame rotation offset applied to the spawned GC so it visually aligns with the original
	 *  static mesh. Fracture Editor sometimes bakes its own orientation into the GC asset (typical
	 *  cases: ±90° around X or Z). Tweak per BP_TurretBuilding subclass until gibs match the SM. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Building|Collapse|Visual")
	FRotator GCRotationOffset = FRotator::ZeroRotator;

	// ==================== IBuildingMarkable ====================

	virtual void OnMarked_Implementation(FVector HitLocation, FVector HitNormal) override;

	// ==================== Public API ====================

	/** Trigger the collapse (called by the drone on impact, or by gameplay scripting).
	 *  No-op if already dead or already collapsing. */
	UFUNCTION(BlueprintCallable, Category = "Building")
	void Collapse(FVector ImpactLocation);

	// ==================== AActor Overrides ====================

	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:

	/** Custom progressive top-down collapse instead of the parent's "instant max-strain everywhere" pattern. */
	virtual void SpawnDestructionGC(const FVector& DestructionOrigin) override;

	/** Timer tick: unanchor the next horizontal slice from top to bottom over CollapseDuration. */
	UFUNCTION()
	void TickProgressiveCollapse();

private:

	/** The drone we spawned for our current mark. Weak — when destroyed (after impact),
	 *  the building can be marked again. Resets naturally across checkpoint respawns. */
	UPROPERTY()
	TWeakObjectPtr<AKamikazeDroneNPC> SpawnedDrone;

	/** Reentrancy guard for Collapse() (parent's TakeDamage will recurse back through us). */
	bool bCollapsing = false;

	// ==================== Cascade Collapse State ====================

	/** GC component spawned by our overridden SpawnDestructionGC. Weak — parent's ResetProp may destroy the actor on respawn. */
	UPROPERTY()
	TWeakObjectPtr<UGeometryCollectionComponent> CollapseGCComp;

	/** World time when the cascade started. */
	float CollapseStartTime = 0.0f;

	/** Cached world-space Z bounds of the building at collapse start (top and bottom). */
	float CollapseTopZ = 0.0f;
	float CollapseBottomZ = 0.0f;

	/** Highest Z that is still anchored. Decreases each tick as the cascade descends. */
	float CurrentAnchorTopZ = 0.0f;

	/** Cascade timer handle. */
	FTimerHandle CascadeTimerHandle;

	/** Pick the best spawn point by dot(WallNormal, dir to spawn point). Returns nullptr if none valid. */
	ATargetPoint* PickBestDroneSpawnPoint(const FVector& HitLocation, const FVector& HitNormal) const;
};
