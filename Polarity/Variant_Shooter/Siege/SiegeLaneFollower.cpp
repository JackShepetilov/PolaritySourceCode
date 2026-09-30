// SiegeLaneFollower.cpp

#include "SiegeLaneFollower.h"

#include "Engine/World.h"
#include "NavigationSystem.h"
#include "SiegeLane.h"

USiegeLaneFollower::USiegeLaneFollower()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USiegeLaneFollower::SetLane(ASiegeLane* InLane)
{
	Lane = InLane;
	bLaneWalked = false;
	Progress = 0.0f;
	if (InLane && GetOwner())
	{
		Progress = InLane->GetDistanceClosestTo(GetOwner()->GetActorLocation());
	}
}

bool USiegeLaneFollower::Advance(const FVector& From)
{
	const ASiegeLane* const LanePtr = Lane.Get();
	if (!LanePtr || bLaneWalked)
	{
		return false;
	}
	Progress = FMath::Max(Progress, LanePtr->GetDistanceClosestTo(From));
	if (LanePtr->GetLength() - Progress <= EndDistance)
	{
		bLaneWalked = true;
		UE_LOG(LogTemp, Log, TEXT("[LANE_DEBUG] %s walked lane %s, on to the core"),
			*GetNameSafe(GetOwner()), *LanePtr->LaneName.ToString());
		return false;
	}
	return true;
}

bool USiegeLaneFollower::GetMarchGoal(const FVector& From, FVector& OutGoal)
{
	if (!Advance(From))
	{
		return false;
	}
	const ASiegeLane* const LanePtr = Lane.Get();
	OutGoal = LanePtr->GetLocationAtDistance(Progress + Lookahead);

	// The spline is drawn by hand and floats or sinks a little along the ground; the move wants a
	// point on the navmesh. No navmesh near it is not fatal, MoveTo projects on its own.
	if (UNavigationSystemV1* const NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation Nav;
		if (NavSys->ProjectPointToNavigation(OutGoal, Nav, FVector(400.0f, 400.0f, 1500.0f)))
		{
			OutGoal = Nav.Location;
		}
	}
	return true;
}

bool USiegeLaneFollower::IsWithinLeash(const AActor* Target) const
{
	const ASiegeLane* const LanePtr = Lane.Get();
	const UWorld* const World = GetWorld();
	if (WalkerLeashRadius <= 0.0f || !Target || !LanePtr || bLaneWalked || !World)
	{
		return true;
	}
	const float Now = World->GetTimeSeconds();
	LeashAnswers.RemoveAll([](const FLeashAnswer& A) { return !A.Target.IsValid(); });
	FLeashAnswer* Answer = LeashAnswers.FindByPredicate([Target](const FLeashAnswer& A) { return A.Target.Get() == Target; });
	if (Answer && Now - Answer->CheckedAt < 0.25f)
	{
		return Answer->bInside;
	}

	// Two spline lookups, four times a second per creep and target: cheap next to the pathing.
	const FVector Where = Target->GetActorLocation();
	const FVector OnLane = LanePtr->GetLocationAtDistance(LanePtr->GetDistanceClosestTo(Where));
	const float Off = FVector::Dist2D(OnLane, Where);
	const bool bInside = Off <= WalkerLeashRadius;
	if (!Answer)
	{
		Answer = &LeashAnswers.AddDefaulted_GetRef();
		Answer->Target = Target;
	}
	else if (Answer->bInside && !bInside)
	{
		UE_LOG(LogTemp, Log, TEXT("[LANE_DEBUG] %s drops %s: %.0f m off lane %s, leash %.0f m"),
			*GetNameSafe(GetOwner()), *Target->GetName(), Off / 100.0f, *LanePtr->LaneName.ToString(), WalkerLeashRadius / 100.0f);
	}
	Answer->CheckedAt = Now;
	Answer->bInside = bInside;
	return bInside;
}

bool USiegeLaneFollower::GetFlightGoal(const FVector& From, FVector& OutGoal)
{
	if (!Advance(From))
	{
		return false;
	}
	OutGoal = Lane->GetLocationAtDistance(Progress + Lookahead);
	return true;
}
