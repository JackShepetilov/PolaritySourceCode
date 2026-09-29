// SiegeLaneFollower.cpp

#include "SiegeLaneFollower.h"

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

bool USiegeLaneFollower::GetFlightGoal(const FVector& From, FVector& OutGoal)
{
	if (!Advance(From))
	{
		return false;
	}
	OutGoal = Lane->GetLocationAtDistance(Progress + Lookahead);
	return true;
}
