// CoverFinderComponent.h
//
// Finding cover, and nothing else. No behaviour lives here on purpose: design doc 12.7 asks for the
// search to be built and looked at with debug drawing BEFORE anything acts on it, because a cover
// system that picks bad spots and a behaviour that uses them badly are indistinguishable from the
// outside once both exist at once.
//
// What "cover" means here is deliberately small. Not a Gears-style system of marked surfaces with
// normals and slots - that is what you need when a PLAYER occupies cover and the system has to be
// driven. Only the AI uses this, so the whole abstraction is a pair of points (design doc 5.1):
//
//   H (hide) - a spot on the navmesh that the dangerous players cannot see
//   P (peek) - a spot beside it that CAN see the current target
//
// H -> P -> H is the entire behaviour, and nothing here needs to know a wall exists. The one number
// that separates "leaned out from behind a corner" from "ran to a different room" is how far P is
// allowed to be from H.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "CoverFinderComponent.generated.h"

class UEnvQuery;

/** A hide/peek pair, plus what it cost to stand there. */
USTRUCT(BlueprintType)
struct FCoverSpot
{
	GENERATED_BODY()

	/** Where the NPC hides. On the navmesh. */
	UPROPERTY(BlueprintReadOnly, Category = "Cover")
	FVector HideLocation = FVector::ZeroVector;

	/** Where it steps out to shoot. Within PeekStepDistance of HideLocation, which is what makes it
	 *  a corner rather than a second position. */
	UPROPERTY(BlueprintReadOnly, Category = "Cover")
	FVector PeekLocation = FVector::ZeroVector;

	/** Sum of the threat of every player who can see HideLocation. Zero means hidden from everybody;
	 *  a non-zero minimum means hidden from the dangerous ones and open to somebody harmless, which
	 *  is the actual goal (design doc 5.3). */
	UPROPERTY(BlueprintReadOnly, Category = "Cover")
	float Exposure = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cover")
	bool bValid = false;

	/** Low cover: a standing body here is seen, a crouched one is not. The NPC waits crouched. */
	UPROPERTY(BlueprintReadOnly, Category = "Cover")
	bool bLowCover = false;

	/** The peek is taken over the top, in place: PeekLocation IS HideLocation and "stepping out"
	 *  means standing up. Only ever set on low cover whose standing muzzle clears the wall. */
	UPROPERTY(BlueprintReadOnly, Category = "Cover")
	bool bPeekOver = false;
};

/** Fired when a cover search finishes. bFound is false when nothing survived the probes, which is a
 *  normal outcome on an open arena and not an error. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCoverSearchFinished, bool, bFound, FCoverSpot, Cover);

UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class POLARITY_API UCoverFinderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCoverFinderComponent();

	// ==================== Search ====================

	/** Start a search. Asynchronous: the EQS runs on the engine's budget and the result arrives in
	 *  OnCoverSearchFinished, which is the entire reason the design picked EQS for the candidate
	 *  sweep - ten NPCs losing their shields in the same second must not spike the frame.
	 *
	 *  Refuses while a search is already running, and while the requery cooldown is still counting.
	 *  Returns whether a search actually started. */
	UFUNCTION(BlueprintCallable, Category = "AI|Cover")
	bool RequestCover(AActor* Target);

	/** Lean the NEXT search toward a point: a bound forward or round the flank, where the question is
	 *  "the best corner over there" and not "the best corner". Every metre a candidate lies from Point
	 *  costs WeightPerMetre of rank (one unit of rank is one hostile seeing the spot), and candidates
	 *  within MinMoveDistance of the owner are skipped, so the answer is a move and not the corner it
	 *  already stands at. Used up by one search; a search that finds nothing leaves the current
	 *  corner as it was. */
	void SetSearchBias(const FVector& Point, float WeightPerMetre, float MinMoveDistance, float MinTargetDistance = -1.0f);

	/** Has the one this corner was taken against walked round it: its wall no longer stands between
	 *  the two. The one reason to leave a corner at once, whatever else is true of it. */
	UFUNCTION(BlueprintPure, Category = "AI|Cover")
	bool IsFlanked() const;

	/** Take the bias back: the search it was set for did not start. */
	void ClearSearchBias() { bHasSearchBias = false; }

	/** True while an EQS request is outstanding. */
	UFUNCTION(BlueprintPure, Category = "AI|Cover")
	bool IsSearching() const { return bSearchInFlight; }

	UFUNCTION(BlueprintPure, Category = "AI|Cover")
	bool HasCover() const { return CurrentCover.bValid; }

	UFUNCTION(BlueprintPure, Category = "AI|Cover")
	const FCoverSpot& GetCover() const { return CurrentCover; }

	/** Drop the current spot and tell the coordinator the claim is free.
	 *
	 *  Must be called on EVERY exit, including death and pool recycling. The classic way to break
	 *  this system is to leak claims: phantom occupied corners accumulate over a fight and slowly
	 *  squeeze the NPCs out into the open, and nothing about the symptom points at the cause
	 *  (design doc 5.5). */
	UFUNCTION(BlueprintCallable, Category = "AI|Cover")
	void ReleaseCover();

	/** Extra threat actors (anything hostile that is NOT a pawn: a sentry turret, a gun platform)
	 *  that count against a spot's exposure. Pawns are gathered automatically; these come from the
	 *  behaviour that knows the threat, and are held weak so a destroyed turret stops counting on
	 *  its own. Passing an empty array clears them. */
	UFUNCTION(BlueprintCallable, Category = "AI|Cover")
	void SetExtraObservers(const TArray<AActor*>& InObservers);

	/** Recompute the exposure of the spot the NPC is standing in. Players move, so a good H stops
	 *  being one without anything happening to the NPC. Cheap: one trace per living player. */
	UFUNCTION(BlueprintCallable, Category = "AI|Cover")
	float EvaluateCurrentExposure() const;

	/** True when the current spot still hides from whoever matters. */
	UFUNCTION(BlueprintPure, Category = "AI|Cover")
	bool IsCoverStillGood() const;

	/** Has this player taken this corner away from us.
	 *
	 *  Both ends have to be seen, and that is the definition rather than a convenience: a player who
	 *  can see only the hide end has merely pinned the NPC behind it, which is what cover is FOR,
	 *  and one who can see only the peek end has done nothing at all, because that end is meant to
	 *  be seen. It is seeing BOTH that leaves nowhere to stand and nowhere to shoot from, and that
	 *  is the thing worth relocating over.
	 *
	 *  Deliberately per-player rather than a total: the squad needs to know WHO opened it up in
	 *  order to answer with suppression, and a summed exposure cannot name anybody. */
	UFUNCTION(BlueprintPure, Category = "AI|Cover")
	bool IsCoverOpenedBy(const APawn* Player) const;

	/** Seconds until RequestCover will be allowed again. Zero when it is allowed now. */
	UFUNCTION(BlueprintPure, Category = "AI|Cover")
	float GetRequeryCooldownRemaining() const;

	UPROPERTY(BlueprintAssignable, Category = "AI|Cover")
	FOnCoverSearchFinished OnCoverSearchFinished;

	// ==================== Parameters (design doc 5.10, all test values) ====================

	/** The candidate sweep. Points on the navmesh in a ring around the NPC - around the NPC and not
	 *  around the target, because what it is leaving is where it currently stands. Filtered by
	 *  distance to the target and by reachability; NO trace tests, because weighting visibility by
	 *  per-player threat is not something the built-in Trace test can express. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover")
	TObjectPtr<UEnvQuery> CoverQuery;

	/** How far to the side P may be from H. This one number is the whole definition of a corner: if
	 *  the nearest spot with a line to the target is eight metres away, that is a change of position,
	 *  not a peek, and the candidate is thrown away. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "10.0"))
	float PeekStepDistance = 150.0f;

	/** Shooting OVER low cover, the soldier stands this close to the wall (cm from the wall to the
	 *  middle of its body), not CoverStandOff back from it. With the barrel a metre long and the
	 *  body 75 cm back, the muzzle is over the top of the wall, and aimed down at somebody close it
	 *  goes into it: measured 2026-10-12, a sniper on a nest 3 m up could not fire at a player
	 *  nearer than about 15 m. At 40 the muzzle is past the wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0.0", Units = "cm"))
	float PeekOverStandOff = 40.0f;

	/** Never hide closer than this to the target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0.0"))
	float MinPeekDistance = 800.0f;

	/** Never hide further than this from the target. It was 2500, and that was the reason so many
	 *  soldiers "stood in the open": the sides of a bench fight start 28 to 37 m apart, so until
	 *  somebody had walked inside 25 m EVERY corner on the field was refused, the one a soldier
	 *  stood behind included (recorded 2026-10-11: an outpost's garrison at its own windows, 60
	 *  candidates a search, none admitted, "NO COVER, shooting from the open"). Now past the sight
	 *  radius (35 m): a soldier that can see somebody can take cover from him. One told to hold a
	 *  ground has no distance limits at all, near or far. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0.0"))
	float MaxPeekDistance = 4000.0f;

	/** How near another soldier's corner one that holds a ground may take its own (cm). The usual
	 *  radius (the coordinator's CoverBlockRadius, 5 m) keeps soldiers off each other's corners in
	 *  the field; in a yard 20 m across two taken corners closed half of one wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0.0", Units = "cm"))
	float HoldClaimRadius = 180.0f;

	/** Cover is something solid to stand behind: a spot counts only when a wall stands within this
	 *  distance of it on the side of the target (cm). Without it a patch of open floor that some far
	 *  wall happens to hide is "cover", and an enemy stands in the middle of nowhere believing it has
	 *  a corner. The usual rule in shipped cover systems (a cover point is a point against geometry,
	 *  valid against a threat while that geometry lies between the two). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "30.0"))
	float MaxCoverWallDistance = 200.0f;

	/** The query hands back a grid of floor, and almost none of a grid lies against a wall. So each
	 *  point looks this far toward the target for something solid and, finding it, becomes the spot
	 *  right behind it (cm). A point with nothing solid within this distance is open ground and is
	 *  dropped. This is what makes the candidates cover POINTS: places against geometry, found from
	 *  the geometry, the way a baked cover system has them, without the bake. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0.0"))
	float CoverSnapDistance = 500.0f;

	/** How far behind the wall the snapped spot stands (cm). The capsule radius plus a hand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "30.0"))
	float CoverStandOff = 75.0f;

	/** A corner is for shooting from: its peek point should look at where the enemy has been seen, not
	 *  only at the one enemy this search was asked about, who will have stepped aside by the time the
	 *  NPC gets there. Rank taken off a corner whose peek sees every place the side knows an enemy at
	 *  (UFactionContactMemory); one that sees none of them gets nothing. One unit of rank is one
	 *  hostile seeing the hide end, so at 1.5 a corner that controls the area beats one that hides a
	 *  little better and looks at a wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0.0"))
	float ControlWeight = 1.5f;

	/** How many corners that passed the probe are compared before one is taken. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "1"))
	int32 MaxCornersToCompare = 4;

	/** How many of the cheapest candidates get the peek probe. Exposure is a handful of traces per
	 *  candidate; the probe is another handful, so it runs only on the ones that already won on
	 *  exposure (design doc 5.4). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "1"))
	int32 MaxCandidatesToProbe = 10;

	// ==================== Holding ground ====================

	/** How much the friendly/hostile presence at a spot counts against its exposure. One unit here
	 *  trades against one hostile able to see the spot, so 1.5 means "being with the squad is worth
	 *  about one and a half extra pairs of eyes on me". Zero restores pure exposure picking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Space", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float SpaceScoreWeight = 1.5f;

	/** Pull toward teammates */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Space", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float AllyCohesionWeight = 1.0f;

	/** Push away from the enemy formation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Space", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float EnemyAvoidWeight = 1.0f;

	/** Floor on how often a full search may run. The most important number here: if invalidation
	 *  keeps firing because the players are running around the arena, this is the only thing between
	 *  the fight and a flood of queries (design doc 5.9). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0.1"))
	float CoverRequeryCooldown = 2.0f;

	/** Exposure above which the current spot counts as lost. Kept above zero so that a spot which
	 *  only the harmless player can see is not thrown away. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0.0"))
	float CoverLostExposureThreshold = 0.5f;

	/** Eye height above the navmesh point used for the visibility traces, and the height on a player
	 *  those traces aim at. Both measured from the capsule rather than the actor: actor location
	 *  drifts by ten-odd centimetres during animations (AI_PERCEPTION_INSIGHTS.md) and these tests
	 *  work at exactly that scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover")
	float EyeHeight = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover")
	float TargetChestHeight = 40.0f;

	/** A player counts as seeing a point if ANY sample on a ring of this radius around them sees it,
	 *  not just their chest.
	 *
	 *  Aiming a single trace at one point makes a player disappear the instant they step behind a
	 *  corner, and the cover system then declares everything on the far side of that corner safe -
	 *  including ground two steps from the player, which an NPC will happily walk onto. Treating a
	 *  player as occupying a volume rather than a point removes the whole class of "it stopped
	 *  seeing me the moment I clipped the wall, and moved in next to me".
	 *
	 *  Zero falls back to the single chest trace. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0.0"))
	float PlayerRingRadius = 150.0f;

	/** How many samples the ring above is made of. Four is a cross around the player, which already
	 *  covers the case this exists for; more only matters for very thin occluders. Cost is this many
	 *  traces per player per candidate, so it multiplies the most expensive loop in the sweep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "1", ClampMax = "8"))
	int32 PlayerRingSamples = 4;

	/** Draw what the search found: candidates, the chosen H, its P, and the sight lines that decided
	 *  it. This is how 12.7 gets verified before any behaviour reads the result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Debug")
	bool bDrawDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Debug", meta = (ClampMin = "0.0"))
	float DebugDrawDuration = 3.0f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** EQS came back. Everything expensive happens here, once, off the back of an async result. */
	void OnQueryFinished(TSharedPtr<FEnvQueryResult> Result);

	/** Sum of the threat of every living player with a line to this point, plus 1 per extra observer
	 *  that sees it. bCrouched asks about a crouched body at the point instead of a standing one. */
	float ComputeExposure(const FVector& Point, const TArray<APawn*>& Observers, const FCollisionQueryParams& Params,
		bool bCrouched = false) const;

	/** The owner's body heights over a navmesh point: chest and head, standing or crouched, from its
	 *  capsule and its movement component's crouched height. */
	void GetBodySampleHeights(bool bCrouched, float& OutChest, float& OutHead, float& OutSideReach) const;

	/** Is there a wall within MaxCoverWallDistance of Point in the direction Toward, at the chest of
	 *  a standing (or crouched) body there. */
	bool HasSolidCoverToward(const FVector& Point, const FVector& Toward, bool bCrouched, const FCollisionQueryParams& Params) const;

	/** For low cover: can a standing muzzle at the hide spot itself see the target over the wall. */
	bool CanShootOverFrom(const FVector& HideLocation, const FCollisionQueryParams& Params) const;

	/** Try to find a P beside this H. Returns false when there is no corner here, which is what
	 *  disqualifies a candidate that is merely far away behind something.
	 *
	 *  Probes both perpendiculars. If both work, the one with the lower exposure to the OTHER
	 *  players wins - that is "peek round one corner at a time", and it falls out of numbers that
	 *  have already been computed rather than needing logic of its own (design doc 5.5a). */
	bool ProbePeekLocation(const FVector& HideLocation, const TArray<APawn*>& Observers,
		const FCollisionQueryParams& Params, FVector& OutPeek) const;

	/** Line of sight between two world points. Params carries the pawn ignore list, built once per
	 *  search: a single sweep can run close to two hundred traces, and rebuilding the list inside
	 *  each of them turned an O(traces) job into O(traces x pawns) for no reason. */
	bool HasLineOfSight(const FVector& From, const FVector& To, const FCollisionQueryParams& Params) const;

	/** Where the owner thinks Enemy is. False when it knows nothing of him: nobody hides from
	 *  somebody it does not know of. @see AShooterAIController::GetKnownPlaceOf */
	bool GetKnownPlace(const AActor* Enemy, FVector& OutPlace) const;

	/** The same for the target of the search, which is always somewhere. */
	FVector GetTargetPlace(const AActor* Target) const;

	/** Whether Player can see Point, treating the player as a volume rather than a point. One trace
	 *  at their chest plus PlayerRingSamples around them at PlayerRingRadius; any hit counts as
	 *  seen. This is THE visibility question for the whole component - exposure, the peek probe and
	 *  the cover recheck all go through it, so none of them can disagree about what "sees" means. */
	bool CanPlayerSee(const APawn* Player, const FVector& Point, const FCollisionQueryParams& Params,
		bool bCrouched = false) const;

	/** Whether a non-pawn threat actor can see Point. The observer's own body must be ignored:
	 *  a turret's eye is inside its own bounds, and without the ignore the trace would be blocked
	 *  by the observer itself on every line. */
	bool CanActorSee(const AActor* Observer, const FVector& Point, const FCollisionQueryParams& Params,
		bool bCrouched = false) const;

	/** Self plus every pawn. Bodies are not cover: a teammate standing on the sight line does not
	 *  make a corner safe, and counting them would make the choice flicker as people walk past. */
	void BuildTraceParams(FCollisionQueryParams& OutParams) const;

	/** Everyone this NPC is hiding from: every pawn hostile to the owner, players included. Never
	 *  "the player", and no longer "the players" either: an NPC of another faction is somebody to
	 *  take cover from in exactly the same way. */
	void GatherObservers(TArray<APawn*>& OutObservers) const;

	/** Threat weight for one observer, via the coordinator so class and situational threat stay one
	 *  number shared with target selection. */
	float GetThreatFor(APawn* Observer) const;

	void DrawDebugForResult(const TArray<FVector>& Candidates, const TArray<APawn*>& Observers,
		const FCollisionQueryParams& Params) const;

	UPROPERTY()
	FCoverSpot CurrentCover;

	/** Who the peek has to be able to see. Held weakly: a search outlives the frame it started on
	 *  and the target can die inside that window. */
	TWeakObjectPtr<AActor> SearchTarget;

	bool bSearchInFlight = false;
	float LastQueryTime = -1000.0f;

	/** Set while this component holds a claim, so ReleaseCover can be called unconditionally from
	 *  every exit path without the coordinator seeing spurious releases. */
	bool bHoldsClaim = false;

	/** @see SetSearchBias */
	bool bHasSearchBias = false;
	FVector SearchBiasPoint = FVector::ZeroVector;
	float SearchBiasWeightPerMetre = 0.0f;
	float SearchBiasMinMove = 0.0f;

	/** Replaces MinPeekDistance for the biased search when zero or above: an assault closes further. */
	float SearchBiasMinTargetDistance = -1.0f;

	/** Non-pawn threats whose sight counts against exposure, see SetExtraObservers. */
	TArray<TWeakObjectPtr<AActor>> ExtraObservers;
};
