// KamikazeCarrierDrone.h
// Flying drone that carries kamikaze munitions under its belly and drops them on a target.

#pragma once

#include "CoreMinimal.h"
#include "FlyingDrone.h"
#include "KamikazeCarrierDrone.generated.h"

class AKamikazeDroneNPC;
class UBoxComponent;
class UNiagaraSystem;

/**
 * Carrier variant of the flying drone: a bay of kamikaze drones ejected downward, which then join
 * the strike schedule like any other kamikaze. The munitions are launched through
 * AKamikazeDroneNPC::LaunchAsHomingMunition, so the carrier never touches their tuning: that lives
 * in the payload Blueprint.
 *
 * Self-driven (default): holds a standoff point on the side it came from, out of close range but in
 * reach of the player's guns, swings across its sector now and then, and drops while in position,
 * in sight of its target, and while that target is not already busy with MaxDronesPerTarget drones.
 * It does not shoot and does not leave; with bInfinitePayload the swarm lasts as long as it does.
 * The drone StateTree is stopped.
 *
 * Not self-driven: the old behavior. The drone StateTree flies and shoots, and either
 * FSTTask_DroneDeployKamikaze or the bAutoDeploy fallback drops.
 */
UCLASS()
class POLARITY_API AKamikazeCarrierDrone : public AFlyingDrone
{
	GENERATED_BODY()

public:

	AKamikazeCarrierDrone(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Drop one munition now, ignoring the salvo cooldown. False when the bay is empty or this is
	 *  not the authority. */
	UFUNCTION(BlueprintCallable, Category = "Carrier")
	bool DeployKamikaze();

	/** Start a salvo: DronesPerSalvo munitions spaced by DeployInterval, then the cooldown.
	 *  False when a salvo is already running, the cooldown is not up, or the bay is empty. */
	UFUNCTION(BlueprintCallable, Category = "Carrier")
	bool DeploySalvo();

	/** True when a salvo would actually start right now (alive, loaded, off cooldown, not busy). */
	UFUNCTION(BlueprintPure, Category = "Carrier")
	bool CanDeploySalvo() const;

	/** Munitions still in the bay. */
	UFUNCTION(BlueprintPure, Category = "Carrier")
	int32 GetPayloadRemaining() const { return PayloadRemaining; }

	/** Seconds until the next salvo is allowed, 0 when it is allowed now. */
	UFUNCTION(BlueprintPure, Category = "Carrier")
	float GetSalvoCooldownRemaining() const;

	/** True while a salvo still owes munitions on the drop timer. */
	UFUNCTION(BlueprintPure, Category = "Carrier")
	bool IsSalvoInProgress() const { return PendingInSalvo > 0; }

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// ==================== Payload ====================

	/** Blueprint of the munition. Its own defaults carry the homing tuning (turn rate, lock-out). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Payload")
	TSubclassOf<AKamikazeDroneNPC> PayloadDroneClass;

	/** How many munitions the carrier holds for its whole life. Not refilled. Ignored while
	 *  bInfinitePayload is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Payload", meta = (ClampMin = "1", ClampMax = "32"))
	int32 PayloadCapacity = 4;

	/** Munitions released per salvo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Payload", meta = (ClampMin = "1", ClampMax = "8"))
	int32 DronesPerSalvo = 2;

	/** Gap between munitions inside one salvo (seconds). Small: this is the drop rhythm, not a
	 *  cooldown, and a salvo that lands all at once reads as one object rather than several. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Payload", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float DeployInterval = 0.35f;

	/** Cooldown between salvos (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Payload", meta = (ClampMin = "1.0"))
	float SalvoCooldown = 15.0f;

	// ==================== Behavior ====================
	// The carrier is a delivery platform, not a gunship: it stands off out of close range, drops its
	// kamikaze drones, and does nothing else. It does not shoot and it does not leave: the flow of
	// drones stops when the carrier dies, so killing it is the answer to the swarm.

	/** Drive itself: hold a standoff point and drop on its own. The drone StateTree (fire positions,
	 *  shooting, evasive dashes) is stopped. Off = the old behavior, the StateTree and bAutoDeploy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Behavior")
	bool bSelfDriven = true;

	/** Never runs out while alive: the swarm lasts as long as the carrier does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Behavior")
	bool bInfinitePayload = true;

	/** Horizontal distance from its target it holds at (cm). Within reach of the player's guns: the
	 *  carrier must be honestly killable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Behavior", meta = (ClampMin = "500", Units = "cm"))
	float StandoffDistance = 4000.0f;

	/** Height of the standoff point above the target's feet (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Behavior", meta = (ClampMin = "0", Units = "cm"))
	float StandoffHeight = 1500.0f;

	/** Within this of the standoff point it counts as in position and may drop (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Behavior", meta = (ClampMin = "50", Units = "cm"))
	float StandoffTolerance = 600.0f;

	/** Every this many seconds it moves to the other side of its sector, so it has to be found again.
	 *  Zero = never. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Behavior", meta = (ClampMin = "0", Units = "s"))
	float RepositionInterval = 8.0f;

	/** How far it moves around its target when it repositions, side to side (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Behavior", meta = (ClampMin = "0", ClampMax = "120"))
	float RepositionAngle = 20.0f;

	/** Seconds a reposition takes. Long on purpose: the carrier drifts across, it does not dash, so a
	 *  player can keep it in the crosshair while it moves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Behavior", meta = (ClampMin = "0.1", Units = "s"))
	float RepositionTime = 4.0f;

	/** After a drop it hangs still for this long before it may move again: the drop is its window of
	 *  vulnerability, and the moment the player is most likely looking at it (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Behavior", meta = (ClampMin = "0", Units = "s"))
	float HoldStillAfterDrop = 2.0f;

	// ==================== Hitbox ====================

	/** The airframe itself catches rounds (its own collision hulls). While on, Hitbox is disabled:
	 *  bullets and hitscan land straight on the mesh. Requires the mesh asset to have collision
	 *  (simple convex hulls) - without hulls the wings catch nothing, so turn this off to go back
	 *  to the fitted box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Hitbox")
	bool bMeshAsHitbox = true;

	/** What bullets hit. The capsule is the movement body and a sphere; the carrier's mesh is a flat,
	 *  wide airframe, so the capsule missed the arms and caught empty air above the hull. This box
	 *  follows the mesh (it is attached to it, so it tilts with it), is found by weapon traces like
	 *  any pawn, and blocks only ballistic rounds (ECC_WorldDynamic): movement, walls and pawns still
	 *  go by the capsule, exactly as before. Disabled while bMeshAsHitbox is on. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Carrier|Hitbox")
	TObjectPtr<UBoxComponent> Hitbox;

	/** Size the hitbox to the drone mesh's bounds at BeginPlay, so a new mesh or scale needs no retuning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Hitbox")
	bool bFitHitboxToMesh = true;

	/** Extra margin around the mesh when fitting (cm, in world units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Hitbox", meta = (ClampMin = "0", Units = "cm"))
	float HitboxPadding = 10.0f;

	/** It holds its drops while this many kamikaze drones are already on its target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Behavior", meta = (ClampMin = "1", ClampMax = "20"))
	int32 MaxDronesPerTarget = 4;

	// ==================== Deploy Trigger ====================

	/** Deploy without a StateTree state, on cooldown, whenever a hostile is within AutoDeployRange.
	 *  Turn off when FSTTask_DroneDeployKamikaze drives the ability from the tree. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Trigger")
	bool bAutoDeploy = true;

	/** How close a hostile has to be for the automatic trigger (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Trigger", meta = (ClampMin = "500"))
	float AutoDeployRange = 6000.0f;

	/** How often the automatic trigger looks for a hostile (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Trigger", meta = (ClampMin = "0.1"))
	float AutoDeployCheckInterval = 0.5f;

	// ==================== Hardpoints ====================

	/** Sockets on the drone mesh the munitions drop from. Empty = a ring under the hull built from
	 *  HardpointDropOffset and HardpointRingRadius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Hardpoints")
	TArray<FName> HardpointSockets;

	/** Vertical offset of the fallback ring, relative to the actor (cm, negative = below). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Hardpoints")
	float HardpointDropOffset = -70.0f;

	/** Radius of the fallback ring (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Hardpoints", meta = (ClampMin = "0"))
	float HardpointRingRadius = 45.0f;

	// ==================== Launch ====================

	/** Ejection speed of a munition (cm/s). Its own LaunchDecayRate settles it to cruise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Launch", meta = (ClampMin = "0"))
	float LaunchSpeed = 700.0f;

	/** Ejection direction in the carrier's local space (X forward, Y right, Z up), normalized.
	 *  Default is down and slightly forward: the munition clears the hull before it turns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Launch")
	FVector LaunchDirectionLocal = FVector(0.25f, 0.0f, -1.0f);

	/** Random cone half-angle around the launch direction (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|Launch", meta = (ClampMin = "0", ClampMax = "90"))
	float LaunchSpreadAngle = 10.0f;

	// ==================== VFX / SFX ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|VFX")
	TObjectPtr<UNiagaraSystem> DeployVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier|SFX")
	TObjectPtr<USoundBase> DeploySFX;

private:

	/** Munitions left in the bay. */
	int32 PayloadRemaining = 0;

	/** World time the last salvo started; the cooldown runs from there. */
	float LastSalvoTime = -1000.0f;

	/** Munitions still owed by the salvo in progress. */
	int32 PendingInSalvo = 0;

	/** Index of the next hardpoint, so a salvo does not drop everything from one point. */
	int32 NextHardpointIndex = 0;

	/** World time of the last automatic-trigger hostile check. */
	float LastAutoCheckTime = -1000.0f;

	FTimerHandle SalvoTimerHandle;

	/** Timer entry point for the munitions after the first one in a salvo. */
	void DeployNextInSalvo();

	// ---- Self-driven behavior ----

	/** One frame of the self-driven behavior: pick the target, hold the standoff point, drop. */
	void TickSelfDriven(float DeltaTime);

	/** Stop the drone StateTree once, so it does not steer or shoot alongside the self-driven logic. */
	void StopDroneStateTree();

	TWeakObjectPtr<APawn> StandoffTarget;

	/** The base's core, while nobody is defending it (ASiegeCoreBuildable::FindUndefended). Set,
	 *  it replaces the pawn: the carrier stands off from the core and every munition it drops
	 *  dives straight into it. Re-picked with the target, so a player coming home takes it back. */
	TWeakObjectPtr<AActor> SiegeCore;
	float TargetReacquireTimer = 0.0f;
	float StandoffBearingDeg = 0.0f;
	bool bHasStandoffBearing = false;
	float RepositionTimer = 0.0f;
	float RepositionSide = 1.0f;
	float RepositionOffsetDeg = 0.0f;
	float MoveOrderTimer = 0.0f;
	bool bStateTreeStopped = false;

	/** Fit Hitbox to the drone mesh's bounds. */
	void FitHitboxToMesh();

	/** World transform the next munition is spawned at. */
	FTransform GetNextHardpointTransform();
};
