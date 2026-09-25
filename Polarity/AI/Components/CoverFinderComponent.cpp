// CoverFinderComponent.cpp

#include "CoverFinderComponent.h"

#include "AI/PolarityTeams.h"
#include "AI/TacticalSpace.h"
#include "AI/Coordination/AICombatCoordinator.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"

// Filter the Output Log on [COVER_DEBUG] to follow a search end to end.
DEFINE_LOG_CATEGORY_STATIC(LogCover, Log, All);

namespace CoverRoute
{
	/** Closer than this to a threat on the way is running past it. */
	static constexpr float ThreatClearance = 500.0f;

	/** ...but only when the route gets that close IN THE MIDDLE: a corner that is itself near the
	 *  turret (they all are, peek range is 8-25 m) is judged by its exposure, not here. */
	static constexpr float EndpointSlack = 150.0f;

	/** Whether the walk From -> To, as the navmesh routes it, runs past any of Threats: some point of
	 *  it closer than ThreatClearance to a threat and clearly closer than both ends of the walk are.
	 *  "Past" and not merely "near": retreating AWAY from a turret the NPC is standing next to is fine.
	 *  No path at all answers false; the move itself will fail and route to Seeking. */
	static bool RunsPastThreat(UWorld* World, AActor* Querier, const FVector& From, const FVector& To,
		const TArray<const AActor*>& Threats, const AActor*& OutThreat, float& OutClosest)
	{
		UNavigationSystemV1* const NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!NavSys || Threats.Num() == 0)
		{
			return false;
		}
		const UNavigationPath* const Route = NavSys->FindPathToLocationSynchronously(World, From, To, Querier);
		if (!Route || Route->PathPoints.Num() < 2)
		{
			return false;
		}
		for (const AActor* const Threat : Threats)
		{
			if (!Threat)
			{
				continue;
			}
			const FVector At = Threat->GetActorLocation();
			const float EndsClosest = FMath::Min(FVector::Dist2D(From, At), FVector::Dist2D(To, At));
			float Closest = TNumericLimits<float>::Max();
			for (int32 Index = 1; Index < Route->PathPoints.Num(); ++Index)
			{
				const FVector OnSegment = FMath::ClosestPointOnSegment(At, Route->PathPoints[Index - 1], Route->PathPoints[Index]);
				Closest = FMath::Min(Closest, static_cast<float>(FVector::Dist2D(OnSegment, At)));
			}
			if (Closest < ThreatClearance && Closest < EndsClosest - EndpointSlack)
			{
				OutThreat = Threat;
				OutClosest = Closest;
				return true;
			}
		}
		return false;
	}
}

UCoverFinderComponent::UCoverFinderComponent()
{
	// Nothing to tick. Searching is asked for, not polled: the thing that knows a shield just broke
	// is the behaviour, and a component ticking to discover it would be both slower and vaguer.
	PrimaryComponentTick.bCanEverTick = false;
}

void UCoverFinderComponent::BeginPlay()
{
	Super::BeginPlay();

	// A search that has never run must be allowed immediately, so the cooldown starts expired
	// rather than at time zero (which on a level loaded at T=0 is the same thing, and on a pooled
	// NPC recycled at T=300 very much is not).
	LastQueryTime = -CoverRequeryCooldown - 1.0f;
}

void UCoverFinderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// One of the mandatory release paths. The others are death and pool recycling, and they are the
	// owner's job to call - this one only covers the actor actually going away.
	ReleaseCover();

	Super::EndPlay(EndPlayReason);
}

bool UCoverFinderComponent::RequestCover(AActor* Target)
{
	if (!Target || !CoverQuery)
	{
		UE_LOG(LogCover, Verbose, TEXT("[COVER_DEBUG] %s search refused: %s"),
			*GetNameSafe(GetOwner()), Target ? TEXT("no query asset assigned") : TEXT("no target"));
		return false;
	}

	if (bSearchInFlight)
	{
		return false;
	}

	if (GetRequeryCooldownRemaining() > 0.0f)
	{
		return false;
	}

	const UWorld* const World = GetWorld();
	if (!World)
	{
		return false;
	}

	SearchTarget = Target;
	LastQueryTime = World->GetTimeSeconds();
	bSearchInFlight = true;

	// The querier is the owning actor, because the generator rings around it. Note this is the AI
	// controller when the component sits there, so whoever assigns CoverQuery has to make sure the
	// query's contexts agree with that.
	FEnvQueryRequest Request(CoverQuery, GetOwner());
	Request.Execute(EEnvQueryRunMode::AllMatching, this, &UCoverFinderComponent::OnQueryFinished);

	return true;
}

void UCoverFinderComponent::OnQueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
	bSearchInFlight = false;

	// [COVER_DEBUG] TIMING: what one scoring pass costs on the game thread (the EQS part before it runs
	// time-sliced and is not counted). Logged at Log so the bench sees it; one line per search.
	const double TimingStart = FPlatformTime::Seconds();
	ON_SCOPE_EXIT
	{
		UE_LOG(LogCover, Log, TEXT("[COVER_DEBUG] TIMING %s: %.3f ms, %d candidates"),
			*GetNameSafe(GetOwner()), (FPlatformTime::Seconds() - TimingStart) * 1000.0,
			Result.IsValid() ? Result->Items.Num() : 0);
	};

	AActor* const Target = SearchTarget.Get();
	if (!Result.IsValid() || !Result->IsSuccessful() || !Target)
	{
		OnCoverSearchFinished.Broadcast(false, FCoverSpot());
		return;
	}

	TArray<FVector> Candidates;
	Result->GetAllAsLocations(Candidates);

	if (Candidates.Num() == 0)
	{
		UE_LOG(LogCover, Verbose, TEXT("[COVER_DEBUG] %s: query returned nothing"), *GetNameSafe(GetOwner()));
		OnCoverSearchFinished.Broadcast(false, FCoverSpot());
		return;
	}

	TArray<APawn*> Observers;
	GatherObservers(Observers);

	// Built once for the whole sweep, not per trace. See BuildTraceParams.
	FCollisionQueryParams TraceParams;
	BuildTraceParams(TraceParams);

	const AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(GetOwner());
	const FVector TargetLocation = Target->GetActorLocation();

	// ---- Pass one: exposure, and the cheap rejections ----
	//
	// Exposure first and the peek probe second, not the other way round: the probe is the more
	// expensive of the two per candidate, so it should only ever run on candidates that already won
	// (design doc 5.4).
	struct FScoredCandidate
	{
		FVector Location;
		float Exposure;
		float DistanceToNPC;

		/** Friendly presence minus hostile presence at this spot, see TacticalSpace */
		float SpaceScore;

		/** Seen by one of the extra observers (a turret). A tier of its own, see the sort. */
		bool bSeenByExtra;

		/** Hidden only crouched: the numbers above are the crouched ones, and the NPC will wait down. */
		bool bLow;
	};

	const FVector OwnerLocation = GetOwner()->GetActorLocation();
	TArray<FScoredCandidate> Scored;
	Scored.Reserve(Candidates.Num());

	// Who is standing where, once for the whole search. Cover that is safe from fire but sits alone
	// inside the enemy formation is not cover, it is a hole in your own line.
	TacticalSpace::FSpaceContext SpaceContext;
	TacticalSpace::BuildContext(GetOwner(), SpaceContext);

	TacticalSpace::FSpaceWeights SpaceWeights;
	SpaceWeights.AllyWeight = AllyCohesionWeight;
	SpaceWeights.EnemyWeight = EnemyAvoidWeight;

	for (const FVector& Candidate : Candidates)
	{
		const float DistanceToTarget = FVector::Dist2D(Candidate, TargetLocation);
		if (DistanceToTarget < MinPeekDistance || DistanceToTarget > MaxPeekDistance)
		{
			continue;
		}

		// Somebody else's corner. Claimed spots block a radius around themselves so two NPCs do not
		// end up behind the same wall from opposite sides, tripping over each other's peeks.
		if (Coordinator && Coordinator->IsCoverBlocked(Candidate, GetOwner()))
		{
			continue;
		}

		auto SeenByExtraAt = [this, &Candidate, &TraceParams](bool bCrouched)
		{
			for (const TWeakObjectPtr<AActor>& Observer : ExtraObservers)
			{
				if (Observer.IsValid() && CanActorSee(Observer.Get(), Candidate, TraceParams, bCrouched))
				{
					return true;
				}
			}
			return false;
		};

		// Standing first. Only a spot that is seen standing is asked again crouched, so high cover and
		// open ground cost what they did and only the candidates that might be LOW cover pay for the
		// second pass. Low = the crouched body is strictly better off than the standing one.
		float Exposure = ComputeExposure(Candidate, Observers, TraceParams, /*bCrouched*/ false);
		bool bSeenByExtra = SeenByExtraAt(false);
		bool bLow = false;
		if (Exposure > KINDA_SMALL_NUMBER || bSeenByExtra)
		{
			const float CrouchedExposure = ComputeExposure(Candidate, Observers, TraceParams, /*bCrouched*/ true);
			const bool bCrouchedSeenByExtra = SeenByExtraAt(true);
			const bool bBetterTier = bSeenByExtra && !bCrouchedSeenByExtra;
			const bool bBetterSameTier = bSeenByExtra == bCrouchedSeenByExtra && CrouchedExposure < Exposure - KINDA_SMALL_NUMBER;
			if (bBetterTier || bBetterSameTier)
			{
				bLow = true;
				Exposure = CrouchedExposure;
				bSeenByExtra = bCrouchedSeenByExtra;
			}
		}

		Scored.Add({ Candidate,
			Exposure,
			static_cast<float>(FVector::Dist2D(Candidate, OwnerLocation)),
			TacticalSpace::ScorePosition(SpaceContext, Candidate, SpaceWeights),
			bSeenByExtra, bLow });
	}

	if (Scored.Num() == 0)
	{
		UE_LOG(LogCover, Verbose, TEXT("[COVER_DEBUG] %s: %d candidates, none passed distance/claim filters"),
			*GetNameSafe(GetOwner()), Candidates.Num());
		OnCoverSearchFinished.Broadcast(false, FCoverSpot());
		return;
	}

	// Exposure and ground, in one number, with distance breaking ties so an enemy does not cross the
	// arena for a spot no better than the one at its feet. Exposure used to decide alone, which is
	// how NPCs ended up scattered through the enemy squad: a lone covered corner behind their line
	// scored better than a mediocre one next to a teammate.
	const float SpaceScale = SpaceScoreWeight;
	Scored.Sort([SpaceScale](const FScoredCandidate& A, const FScoredCandidate& B)
	{
		// A turret's view is not traded for company. The ground bonus below can outweigh a whole unit
		// of exposure (SpaceScoreWeight 1.5 against a turret's 1.0), and did: on the bench one arrival in
		// five was at a spot the search KNEW the turret saw, chosen over a hidden one because a teammate
		// stood nearer to it. A turret does not flinch at numbers; the hidden spots go first, always.
		if (A.bSeenByExtra != B.bSeenByExtra)
		{
			return !A.bSeenByExtra;
		}

		const float RankA = A.Exposure - SpaceScale * A.SpaceScore;
		const float RankB = B.Exposure - SpaceScale * B.SpaceScore;

		if (!FMath::IsNearlyEqual(RankA, RankB, 0.01f))
		{
			return RankA < RankB;
		}
		return A.DistanceToNPC < B.DistanceToNPC;
	});

	// ---- Pass two: the peek probe, best candidates first ----
	FCoverSpot Chosen;
	const int32 ProbeCount = FMath::Min(Scored.Num(), FMath::Max(1, MaxCandidatesToProbe));

	// Everyone the walk to the corner must not run past: hostile pawns and the extra observers (turrets).
	TArray<const AActor*> RouteThreats;
	RouteThreats.Reserve(Observers.Num() + ExtraObservers.Num());
	for (const APawn* const Observer : Observers)
	{
		RouteThreats.Add(Observer);
	}
	for (const TWeakObjectPtr<AActor>& Observer : ExtraObservers)
	{
		if (const AActor* const Threat = Observer.Get())
		{
			RouteThreats.Add(Threat);
		}
	}

	for (int32 Index = 0; Index < ProbeCount; ++Index)
	{
		// Low cover whose standing muzzle clears the wall is peeked OVER, in place: P is H, and
		// stepping out means standing up. Otherwise (high cover, or a low wall too tall to shoot over
		// from where it stands) the usual side step.
		FVector PeekLocation = FVector::ZeroVector;
		const bool bPeekOver = Scored[Index].bLow && CanShootOverFrom(Scored[Index].Location, TraceParams);
		if (bPeekOver)
		{
			PeekLocation = Scored[Index].Location;
		}
		else if (!ProbePeekLocation(Scored[Index].Location, Observers, TraceParams, PeekLocation))
		{
			// No corner here. Not a failure, just not cover: this is what separates a real angle
			// from open ground that happens to be far away.
			continue;
		}

		// A good corner on the far side of the enemy is not reachable, it is a charge. Checked after
		// the peek probe (the cheaper filter) and only for the handful that pass it: one navmesh query
		// per surviving candidate.
		const AActor* PastThreat = nullptr;
		float PastClosest = 0.0f;
		if (CoverRoute::RunsPastThreat(GetWorld(), GetOwner(), OwnerLocation, Scored[Index].Location, RouteThreats, PastThreat, PastClosest))
		{
			UE_LOG(LogCover, Log, TEXT("[COVER_DEBUG] %s: corner %s refused, the way there passes %.0fcm from %s"),
				*GetNameSafe(GetOwner()), *Scored[Index].Location.ToCompactString(), PastClosest, *GetNameSafe(PastThreat));
			continue;
		}

		Chosen.HideLocation = Scored[Index].Location;
		Chosen.PeekLocation = PeekLocation;
		Chosen.Exposure = Scored[Index].Exposure;
		Chosen.bLowCover = Scored[Index].bLow;
		Chosen.bPeekOver = bPeekOver;
		Chosen.bValid = true;
		break;
	}

	if (bDrawDebug)
	{
		TArray<FVector> DrawCandidates;
		DrawCandidates.Reserve(Scored.Num());
		for (const FScoredCandidate& Entry : Scored)
		{
			DrawCandidates.Add(Entry.Location);
		}
		DrawDebugForResult(DrawCandidates, Observers, TraceParams);
	}

	if (!Chosen.bValid)
	{
		UE_LOG(LogCover, Verbose, TEXT("[COVER_DEBUG] %s: %d candidates scored, none had a valid peek point"),
			*GetNameSafe(GetOwner()), Scored.Num());
		OnCoverSearchFinished.Broadcast(false, FCoverSpot());
		return;
	}

	// Swap claims in this order - release then claim - so a component re-covering to a spot near its
	// old one is not blocked by its own claim.
	ReleaseCover();

	CurrentCover = Chosen;

	if (AAICombatCoordinator* const MutableCoordinator = AAICombatCoordinator::GetCoordinator(GetOwner()))
	{
		MutableCoordinator->ClaimCover(GetOwner(), Chosen.HideLocation);
		bHoldsClaim = true;
	}

	UE_LOG(LogCover, Verbose,
		TEXT("[COVER_DEBUG] %s: chose H=%s exposure=%.2f, P=%s (%d candidates, %d scored)"),
		*GetNameSafe(GetOwner()), *Chosen.HideLocation.ToCompactString(), Chosen.Exposure,
		*Chosen.PeekLocation.ToCompactString(), Candidates.Num(), Scored.Num());

	if (bDrawDebug)
	{
		DrawDebugSphere(GetWorld(), Chosen.HideLocation, 45.0f, 12, FColor::Green, false, DebugDrawDuration, 0, 3.0f);
		DrawDebugSphere(GetWorld(), Chosen.PeekLocation, 30.0f, 12, FColor::Cyan, false, DebugDrawDuration, 0, 3.0f);
		DrawDebugLine(GetWorld(), Chosen.HideLocation, Chosen.PeekLocation, FColor::Cyan, false, DebugDrawDuration, 0, 3.0f);
	}

	OnCoverSearchFinished.Broadcast(true, Chosen);
}

void UCoverFinderComponent::ReleaseCover()
{
	if (bHoldsClaim)
	{
		if (AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(GetOwner()))
		{
			Coordinator->ReleaseCover(GetOwner());
		}
		bHoldsClaim = false;
	}

	CurrentCover = FCoverSpot();
}

void UCoverFinderComponent::SetExtraObservers(const TArray<AActor*>& InObservers)
{
	ExtraObservers.Reset(InObservers.Num());
	for (AActor* const Observer : InObservers)
	{
		if (Observer)
		{
			ExtraObservers.Add(Observer);
		}
	}
}

float UCoverFinderComponent::EvaluateCurrentExposure() const
{
	if (!CurrentCover.bValid)
	{
		return 0.0f;
	}

	TArray<APawn*> Observers;
	GatherObservers(Observers);

	FCollisionQueryParams TraceParams;
	BuildTraceParams(TraceParams);

	// Asked in the pose the NPC waits in: crouched behind low cover, standing behind high.
	return ComputeExposure(CurrentCover.HideLocation, Observers, TraceParams, CurrentCover.bLowCover);
}

bool UCoverFinderComponent::IsCoverStillGood() const
{
	if (!CurrentCover.bValid)
	{
		return false;
	}

	// Judged against what this spot was worth WHEN IT WAS CHOSEN, not against an absolute ideal.
	//
	// The search never refuses a bad map: it ranks the candidates and takes the best one that has a
	// peek, however exposed that winner is. On open ground covered by a turret every candidate is
	// seen, so the winner legitimately scores above CoverLostExposureThreshold - and comparing it
	// against that threshold made this return false for the very spot the search had just handed
	// over. The caller reads that as "cover lost" and goes back to searching, the search returns the
	// same spot, and the NPC bounces on the recheck cadence for ever.
	//
	// Measured 2026-09-22 in a live siege: 143 Seeking -> ToHide transitions for 12 engagements, at
	// exactly the 0.5s recheck interval, with an identical H and P every time. From outside it looks
	// like an enemy pacing in and out of a corner under fire without ever shooting back.
	//
	// So the question this answers is "has it got WORSE" - which is the thing actually worth
	// relocating over, because that means somebody has walked around and opened the corner up. A
	// spot that is merely as exposed as it was when nothing better existed stays usable, and the NPC
	// fights from it instead of freezing.
	return EvaluateCurrentExposure() <= FMath::Max(CoverLostExposureThreshold, CurrentCover.Exposure);
}

float UCoverFinderComponent::GetRequeryCooldownRemaining() const
{
	const UWorld* const World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}

	const float Elapsed = World->GetTimeSeconds() - LastQueryTime;
	return FMath::Max(0.0f, CoverRequeryCooldown - Elapsed);
}

void UCoverFinderComponent::GetBodySampleHeights(bool bCrouched, float& OutChest, float& OutHead, float& OutSideReach) const
{
	// A BODY at the spot, not one point at EyeHeight. EyeHeight (60 on the shooters) is knee height:
	// a low wall hides it and leaves the chest and the head in the open. Measured 2026-09-23 on the
	// turret bench: a quarter of all arrivals at a spot scored hidden were in the turret's view.
	// Crouched is the same body at the movement component's crouched height, which is what tells low
	// cover (hidden only crouched) from high cover (hidden standing) from no cover at all.
	const ACharacter* const OwnerCharacter = Cast<ACharacter>(GetOwner());
	const UCapsuleComponent* const Capsule = OwnerCharacter ? OwnerCharacter->GetCapsuleComponent() : nullptr;
	const UCharacterMovementComponent* const Move = OwnerCharacter ? OwnerCharacter->GetCharacterMovement() : nullptr;

	const float StandingHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() * 2.0f : 180.0f;
	const float CrouchedHeight = Move ? Move->GetCrouchedHalfHeight() * 2.0f : StandingHeight * 0.6f;
	const float Height = bCrouched ? CrouchedHeight : StandingHeight;

	OutChest = Height * 0.6f;
	OutHead = Height * 0.92f;
	// The body's half width plus arrival slack: the NPC stops within about a metre of the spot.
	OutSideReach = (Capsule ? Capsule->GetScaledCapsuleRadius() : 34.0f) + 40.0f;
}

bool UCoverFinderComponent::CanPlayerSee(const APawn* Player, const FVector& Point, const FCollisionQueryParams& Params,
	bool bCrouched) const
{
	if (!Player)
	{
		return false;
	}

	float Chest = 0.0f;
	float Head = 0.0f;
	float SideReach = 0.0f;
	GetBodySampleHeights(bCrouched, Chest, Head, SideReach);

	const FVector PlayerBase = Player->GetActorLocation() + FVector(0.0f, 0.0f, TargetChestHeight);
	const FVector ChestPoint = Point + FVector(0.0f, 0.0f, Chest);

	// The chest first, because in the open it answers immediately and nothing else runs.
	if (HasLineOfSight(ChestPoint, PlayerBase, Params))
	{
		return true;
	}

	// Then the rest of the body, straight at the player: the head over the wall, the shoulders round
	// its ends. Three traces, and only for a spot the chest trace called hidden.
	const FVector Across = FVector::CrossProduct(FVector::UpVector, (PlayerBase - Point).GetSafeNormal2D());
	const FVector BodyPoints[] = {
		Point + FVector(0.0f, 0.0f, Head),
		ChestPoint + Across * SideReach,
		ChestPoint - Across * SideReach,
	};
	for (const FVector& BodyPoint : BodyPoints)
	{
		if (HasLineOfSight(BodyPoint, PlayerBase, Params))
		{
			return true;
		}
	}

	if (PlayerRingRadius <= KINDA_SMALL_NUMBER || PlayerRingSamples <= 0)
	{
		return false;
	}

	// Occluded, so ask whether the player is merely CLIPPING the corner rather than genuinely behind
	// it. Without this, one step behind a wall makes a player vanish from the exposure model entirely
	// and the ground right next to them starts scoring as good cover. From the chest only: the ring is
	// about the player's volume, and running it from every body sample would cost four times as much
	// for a question the chest already answers.
	const float StepDeg = 360.0f / static_cast<float>(PlayerRingSamples);
	for (int32 Index = 0; Index < PlayerRingSamples; ++Index)
	{
		const FVector Offset = FRotator(0.0f, StepDeg * Index, 0.0f).Vector() * PlayerRingRadius;
		if (HasLineOfSight(ChestPoint, PlayerBase + Offset, Params))
		{
			return true;
		}
	}

	return false;
}

bool UCoverFinderComponent::CanActorSee(const AActor* Observer, const FVector& Point, const FCollisionQueryParams& Params,
	bool bCrouched) const
{
	if (!Observer)
	{
		return false;
	}

	FVector Origin = FVector::ZeroVector;
	FVector Extent = FVector::ZeroVector;
	Observer->GetActorBounds(true, Origin, Extent, false);

	FCollisionQueryParams LocalParams = Params;
	LocalParams.AddIgnoredActor(Observer);

	// Chest and head over the spot, and the chest a body's width plus the arrival slack to either side
	// across the observer's line. Any one of them seen is seen. @see GetBodySampleHeights
	float Chest = 0.0f;
	float Head = 0.0f;
	float SideReach = 0.0f;
	GetBodySampleHeights(bCrouched, Chest, Head, SideReach);

	const FVector Across = FVector::CrossProduct(FVector::UpVector, (Point - Origin).GetSafeNormal2D());
	const FVector Samples[] = {
		Point + FVector(0.0f, 0.0f, Chest),
		Point + FVector(0.0f, 0.0f, Head),
		Point + Across * SideReach + FVector(0.0f, 0.0f, Chest),
		Point - Across * SideReach + FVector(0.0f, 0.0f, Chest),
	};
	for (const FVector& Sample : Samples)
	{
		if (HasLineOfSight(Origin, Sample, LocalParams))
		{
			return true;
		}
	}
	return false;
}

bool UCoverFinderComponent::CanShootOverFrom(const FVector& HideLocation, const FCollisionQueryParams& Params) const
{
	const AActor* const Target = SearchTarget.Get();
	if (!Target)
	{
		return false;
	}

	// Muzzle height of a standing NPC, a little under the head: what actually has to clear the wall.
	float Chest = 0.0f;
	float Head = 0.0f;
	float SideReach = 0.0f;
	GetBodySampleHeights(/*bCrouched*/ false, Chest, Head, SideReach);
	const FVector Muzzle = HideLocation + FVector(0.0f, 0.0f, Head * 0.85f);

	// The target is not its own obstacle (a building target is solid, and its chest sits inside it).
	FCollisionQueryParams SeeTargetParams = Params;
	SeeTargetParams.AddIgnoredActor(Target);
	return HasLineOfSight(Muzzle, Target->GetActorLocation() + FVector(0.0f, 0.0f, TargetChestHeight), SeeTargetParams);
}

float UCoverFinderComponent::ComputeExposure(const FVector& Point, const TArray<APawn*>& Observers, const FCollisionQueryParams& Params,
	bool bCrouched) const
{
	// Exposure(H) = sum over players of Threat(P) * Visible(H, P). Visible is one or zero; the
	// weighting is what turns "hidden" into "hidden from the ones that matter". A zero means hidden
	// from everybody, and a non-zero minimum means open only to somebody harmless - which is a
	// perfectly good place to stand.
	float Exposure = 0.0f;

	for (APawn* const Player : Observers)
	{
		if (CanPlayerSee(Player, Point, Params, bCrouched))
		{
			Exposure += GetThreatFor(Player);
		}
	}

	// Non-pawn threats count at full weight: a turret either sees the spot or it does not, and while
	// an NPC is playing cover against one, the turret IS the fight.
	for (const TWeakObjectPtr<AActor>& Observer : ExtraObservers)
	{
		if (const AActor* const Threat = Observer.Get())
		{
			if (CanActorSee(Threat, Point, Params, bCrouched))
			{
				Exposure += 1.0f;
			}
		}
	}

	return Exposure;
}

bool UCoverFinderComponent::IsCoverOpenedBy(const APawn* Player) const
{
	if (!CurrentCover.bValid || !Player)
	{
		return false;
	}

	FCollisionQueryParams TraceParams;
	BuildTraceParams(TraceParams);

	// The hide end in the pose it is held in. A peek over the top has no second end: P is H standing,
	// and being seen there is the point of it, so the hide end alone decides.
	const bool bHideSeen = CanPlayerSee(Player, CurrentCover.HideLocation, TraceParams, CurrentCover.bLowCover);
	if (CurrentCover.bPeekOver)
	{
		return bHideSeen;
	}
	return bHideSeen && CanPlayerSee(Player, CurrentCover.PeekLocation, TraceParams);
}

bool UCoverFinderComponent::ProbePeekLocation(const FVector& HideLocation, const TArray<APawn*>& Observers,
	const FCollisionQueryParams& Params, FVector& OutPeek) const
{
	const AActor* const Target = SearchTarget.Get();
	UWorld* const World = GetWorld();
	if (!Target || !World)
	{
		return false;
	}

	UNavigationSystemV1* const NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		return false;
	}

	const FVector TargetLocation = Target->GetActorLocation();
	const FVector TargetChest = TargetLocation + FVector(0.0f, 0.0f, TargetChestHeight);

	FVector ToTarget = (TargetLocation - HideLocation).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero())
	{
		return false;
	}

	// Perpendicular, both ways. Four to eight traces in C++, which is cheaper and far more
	// predictable than a second EQS - and unlike a query it also answers WHICH SIDE the NPC leans
	// out on, which the animation is going to want (design doc 5.2).
	const FVector Side = FVector::CrossProduct(FVector::UpVector, ToTarget).GetSafeNormal();

	bool bFound = false;
	float BestExposure = TNumericLimits<float>::Max();

	for (int32 Sign = -1; Sign <= 1; Sign += 2)
	{
		const FVector Raw = HideLocation + Side * (PeekStepDistance * static_cast<float>(Sign));

		FNavLocation Projected;
		if (!NavSys->ProjectPointToNavigation(Raw, Projected, FVector(60.0f, 60.0f, 120.0f)))
		{
			continue;
		}

		// A peek point that cannot see the target is not a peek point.
		//
		// A STRICT chest trace, deliberately not CanPlayerSee: the ring exists to be pessimistic
		// about being seen, and pessimism flips sign depending on which way the question runs.
		// Assuming a player might see you costs a corner and is safe; assuming you can shoot a
		// player because you can see the air beside them costs the whole peek and is not. Exposure
		// gets the generous answer, marksmanship gets the honest one.
		//
		// The target itself is not an obstacle to seeing it. Pawns were already ignored (Params), but
		// a building target is solid, and TargetChest sits inside its box: without this every probe
		// against a turret or a core would stop on the target's own surface and report "no line".
		FCollisionQueryParams SeeTargetParams = Params;
		SeeTargetParams.AddIgnoredActor(Target);
		if (!HasLineOfSight(Projected.Location + FVector(0.0f, 0.0f, EyeHeight), TargetChest, SeeTargetParams))
		{
			continue;
		}

		// Both sides valid: take the one the rest of the team can see least of. That is "lean out
		// on the angle where only the person you are shooting can see you", and it comes free from
		// numbers this function is computing anyway rather than needing a rule of its own.
		const float SideExposure = ComputeExposure(Projected.Location, Observers, Params);
		if (SideExposure < BestExposure)
		{
			BestExposure = SideExposure;
			OutPeek = Projected.Location;
			bFound = true;
		}
	}

	return bFound;
}

bool UCoverFinderComponent::HasLineOfSight(const FVector& From, const FVector& To, const FCollisionQueryParams& Params) const
{
	const UWorld* const World = GetWorld();
	if (!World)
	{
		return false;
	}

	return !World->LineTraceTestByChannel(From, To, ECC_Visibility, Params);
}

void UCoverFinderComponent::BuildTraceParams(FCollisionQueryParams& OutParams) const
{
	OutParams = FCollisionQueryParams(FName(TEXT("CoverVisibility")), /*bTraceComplex*/ false);
	OutParams.AddIgnoredActor(GetOwner());

	// Pawns are ignored deliberately, and gathered once: a sweep runs on the order of two hundred
	// traces, so doing this inside the trace would multiply the cost by the number of pawns alive.
	for (TActorIterator<APawn> It(const_cast<UWorld*>(GetWorld())); It; ++It)
	{
		OutParams.AddIgnoredActor(*It);
	}
}

void UCoverFinderComponent::GatherObservers(TArray<APawn*>& OutObservers) const
{
	// Everyone this NPC is hiding FROM, which is everyone hostile to it. Players were the whole
	// answer while they were the only other side; with factions a rifleman needs the corner that is
	// hidden from the faction shooting at it, and that is not necessarily the corner hidden from the
	// players.
	PolarityTeams::GatherHostilePawns(GetOwner(), OutObservers);
}

float UCoverFinderComponent::GetThreatFor(APawn* Observer) const
{
	if (const AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(GetOwner()))
	{
		// The same weight target selection uses, so the push walking towards a player and the peek
		// hiding from them are two readings of one number rather than two systems disagreeing.
		return Coordinator->GetThreatFor(Observer);
	}

	// No coordinator: every observer counts the same, so exposure degrades to "how many can see me",
	// which is still a usable ordering.
	return 1.0f;
}

void UCoverFinderComponent::DrawDebugForResult(const TArray<FVector>& Candidates, const TArray<APawn*>& Observers,
	const FCollisionQueryParams& Params) const
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	for (const FVector& Candidate : Candidates)
	{
		const float Exposure = ComputeExposure(Candidate, Observers, Params);

		// Green is hidden from everybody who matters, red is standing in the open. The point of
		// drawing every candidate and not just the winner is that the interesting failure is "it
		// picked the best of a bad set", and that is invisible if only the winner is shown.
		const FColor Colour = Exposure <= KINDA_SMALL_NUMBER
			? FColor(40, 200, 40)
			: FColor(200, FMath::Clamp(static_cast<int32>(200.0f - Exposure * 80.0f), 0, 200), 40);

		DrawDebugPoint(World, Candidate + FVector(0.0f, 0.0f, 20.0f), 14.0f, Colour, false, DebugDrawDuration);
	}
}
