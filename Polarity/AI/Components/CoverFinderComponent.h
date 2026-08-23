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

	/** Never hide closer than this to the target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0.0"))
	float MinPeekDistance = 800.0f;

	/** Never hide further than this. Matches the coordinator's MaxEngagementDistance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0.0"))
	float MaxPeekDistance = 2500.0f;

	/** How many of the cheapest candidates get the peek probe. Exposure is a handful of traces per
	 *  candidate; the probe is another handful, so it runs only on the ones that already won on
	 *  exposure (design doc 5.4). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "1"))
	int32 MaxCandidatesToProbe = 10;

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

	/** Sum of the threat of every living player with a line to this point. */
	float ComputeExposure(const FVector& Point, const TArray<APawn*>& Players, const FCollisionQueryParams& Params) const;

	/** Try to find a P beside this H. Returns false when there is no corner here, which is what
	 *  disqualifies a candidate that is merely far away behind something.
	 *
	 *  Probes both perpendiculars. If both work, the one with the lower exposure to the OTHER
	 *  players wins - that is "peek round one corner at a time", and it falls out of numbers that
	 *  have already been computed rather than needing logic of its own (design doc 5.5a). */
	bool ProbePeekLocation(const FVector& HideLocation, const TArray<APawn*>& Players,
		const FCollisionQueryParams& Params, FVector& OutPeek) const;

	/** Line of sight between two world points. Params carries the pawn ignore list, built once per
	 *  search: a single sweep can run close to two hundred traces, and rebuilding the list inside
	 *  each of them turned an O(traces) job into O(traces x pawns) for no reason. */
	bool HasLineOfSight(const FVector& From, const FVector& To, const FCollisionQueryParams& Params) const;

	/** Whether Player can see Point, treating the player as a volume rather than a point. One trace
	 *  at their chest plus PlayerRingSamples around them at PlayerRingRadius; any hit counts as
	 *  seen. This is THE visibility question for the whole component - exposure, the peek probe and
	 *  the cover recheck all go through it, so none of them can disagree about what "sees" means. */
	bool CanPlayerSee(const APawn* Player, const FVector& Point, const FCollisionQueryParams& Params) const;

	/** Self plus every pawn. Bodies are not cover: a teammate standing on the sight line does not
	 *  make a corner safe, and counting them would make the choice flicker as people walk past. */
	void BuildTraceParams(FCollisionQueryParams& OutParams) const;

	/** Living players, from the coop helper. Never "the player": there are up to four. */
	void GatherPlayers(TArray<APawn*>& OutPlayers) const;

	/** Threat weight for a player, via the coordinator so class and situational threat stay one
	 *  number shared with target selection. */
	float GetThreatFor(APawn* Player) const;

	void DrawDebugForResult(const TArray<FVector>& Candidates, const TArray<APawn*>& Players,
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
};
