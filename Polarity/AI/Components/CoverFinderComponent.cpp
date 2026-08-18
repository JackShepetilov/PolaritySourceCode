// CoverFinderComponent.cpp

#include "CoverFinderComponent.h"

#include "Coop/CoopPlayers.h"
#include "AI/Coordination/AICombatCoordinator.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "NavigationSystem.h"
#include "GameFramework/Pawn.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"

// Filter the Output Log on [COVER_DEBUG] to follow a search end to end.
DEFINE_LOG_CATEGORY_STATIC(LogCover, Log, All);

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

	TArray<APawn*> Players;
	GatherPlayers(Players);

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
	};

	const FVector OwnerLocation = GetOwner()->GetActorLocation();
	TArray<FScoredCandidate> Scored;
	Scored.Reserve(Candidates.Num());

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

		Scored.Add({ Candidate, ComputeExposure(Candidate, Players, TraceParams), FVector::Dist2D(Candidate, OwnerLocation) });
	}

	if (Scored.Num() == 0)
	{
		UE_LOG(LogCover, Verbose, TEXT("[COVER_DEBUG] %s: %d candidates, none passed distance/claim filters"),
			*GetNameSafe(GetOwner()), Candidates.Num());
		OnCoverSearchFinished.Broadcast(false, FCoverSpot());
		return;
	}

	// Lowest exposure wins; nearest breaks the tie, so an enemy does not cross the arena for a spot
	// no better than the one at its feet.
	Scored.Sort([](const FScoredCandidate& A, const FScoredCandidate& B)
	{
		if (!FMath::IsNearlyEqual(A.Exposure, B.Exposure))
		{
			return A.Exposure < B.Exposure;
		}
		return A.DistanceToNPC < B.DistanceToNPC;
	});

	// ---- Pass two: the peek probe, best candidates first ----
	FCoverSpot Chosen;
	const int32 ProbeCount = FMath::Min(Scored.Num(), FMath::Max(1, MaxCandidatesToProbe));

	for (int32 Index = 0; Index < ProbeCount; ++Index)
	{
		FVector PeekLocation = FVector::ZeroVector;
		if (!ProbePeekLocation(Scored[Index].Location, Players, TraceParams, PeekLocation))
		{
			// No corner here. Not a failure, just not cover: this is what separates a real angle
			// from open ground that happens to be far away.
			continue;
		}

		Chosen.HideLocation = Scored[Index].Location;
		Chosen.PeekLocation = PeekLocation;
		Chosen.Exposure = Scored[Index].Exposure;
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
		DrawDebugForResult(DrawCandidates, Players, TraceParams);
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

float UCoverFinderComponent::EvaluateCurrentExposure() const
{
	if (!CurrentCover.bValid)
	{
		return 0.0f;
	}

	TArray<APawn*> Players;
	GatherPlayers(Players);

	FCollisionQueryParams TraceParams;
	BuildTraceParams(TraceParams);

	return ComputeExposure(CurrentCover.HideLocation, Players, TraceParams);
}

bool UCoverFinderComponent::IsCoverStillGood() const
{
	return CurrentCover.bValid && EvaluateCurrentExposure() <= CoverLostExposureThreshold;
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

float UCoverFinderComponent::ComputeExposure(const FVector& Point, const TArray<APawn*>& Players, const FCollisionQueryParams& Params) const
{
	// Exposure(H) = sum over players of Threat(P) * Visible(H, P). Visible is one or zero; the
	// weighting is what turns "hidden" into "hidden from the ones that matter". A zero means hidden
	// from everybody, and a non-zero minimum means open only to somebody harmless - which is a
	// perfectly good place to stand.
	float Exposure = 0.0f;

	const FVector EyePoint = Point + FVector(0.0f, 0.0f, EyeHeight);

	for (APawn* const Player : Players)
	{
		if (!Player)
		{
			continue;
		}

		const FVector PlayerChest = Player->GetActorLocation() + FVector(0.0f, 0.0f, TargetChestHeight);
		if (HasLineOfSight(EyePoint, PlayerChest, Params))
		{
			Exposure += GetThreatFor(Player);
		}
	}

	return Exposure;
}

bool UCoverFinderComponent::ProbePeekLocation(const FVector& HideLocation, const TArray<APawn*>& Players,
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
		if (!HasLineOfSight(Projected.Location + FVector(0.0f, 0.0f, EyeHeight), TargetChest, Params))
		{
			continue;
		}

		// Both sides valid: take the one the rest of the team can see least of. That is "lean out
		// on the angle where only the person you are shooting can see you", and it comes free from
		// numbers this function is computing anyway rather than needing a rule of its own.
		const float SideExposure = ComputeExposure(Projected.Location, Players, Params);
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

void UCoverFinderComponent::GatherPlayers(TArray<APawn*>& OutPlayers) const
{
	OutPlayers.Reset();
	CoopPlayers::GetAll(GetWorld(), OutPlayers);
}

float UCoverFinderComponent::GetThreatFor(APawn* Player) const
{
	if (const AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(GetOwner()))
	{
		// The same weight target selection uses, so the push walking towards a player and the peek
		// hiding from them are two readings of one number rather than two systems disagreeing.
		return Coordinator->GetPlayerThreat(Player);
	}

	// No coordinator: every player counts the same, so exposure degrades to "how many can see me",
	// which is still a usable ordering.
	return 1.0f;
}

void UCoverFinderComponent::DrawDebugForResult(const TArray<FVector>& Candidates, const TArray<APawn*>& Players,
	const FCollisionQueryParams& Params) const
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	for (const FVector& Candidate : Candidates)
	{
		const float Exposure = ComputeExposure(Candidate, Players, Params);

		// Green is hidden from everybody who matters, red is standing in the open. The point of
		// drawing every candidate and not just the winner is that the interesting failure is "it
		// picked the best of a bad set", and that is invisible if only the winner is shown.
		const FColor Colour = Exposure <= KINDA_SMALL_NUMBER
			? FColor(40, 200, 40)
			: FColor(200, FMath::Clamp(static_cast<int32>(200.0f - Exposure * 80.0f), 0, 200), 40);

		DrawDebugPoint(World, Candidate + FVector(0.0f, 0.0f, 20.0f), 14.0f, Colour, false, DebugDrawDuration);
	}
}
