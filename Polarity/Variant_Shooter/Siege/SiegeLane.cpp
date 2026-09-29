// SiegeLane.cpp

#include "SiegeLane.h"

#include "Components/SplineComponent.h"
#include "EngineUtils.h"

ASiegeLane::ASiegeLane()
{
	PrimaryActorTick.bCanEverTick = false;

	Path = CreateDefaultSubobject<USplineComponent>(TEXT("Path"));
	RootComponent = Path;
	// A lane is long; the default 100 cm spline is useless to draw one from. Start it at 50 m so the
	// first drag in the editor already reads as a road.
	Path->ClearSplinePoints(false);
	Path->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, false);
	Path->AddSplinePoint(FVector(5000.0f, 0.0f, 0.0f), ESplineCoordinateSpace::Local, false);
	Path->UpdateSpline();
#if WITH_EDITORONLY_DATA
	Path->EditorUnselectedSplineSegmentColor = FLinearColor(1.0f, 0.35f, 0.1f);
	Path->bShouldVisualizeScale = false;
#endif
}

float ASiegeLane::GetLength() const
{
	return Path ? Path->GetSplineLength() : 0.0f;
}

FVector ASiegeLane::GetLocationAtDistance(float Distance) const
{
	if (!Path)
	{
		return GetActorLocation();
	}
	return Path->GetLocationAtDistanceAlongSpline(FMath::Clamp(Distance, 0.0f, Path->GetSplineLength()), ESplineCoordinateSpace::World);
}

float ASiegeLane::GetDistanceClosestTo(const FVector& Location) const
{
	if (!Path)
	{
		return 0.0f;
	}
	const float Key = Path->FindInputKeyClosestToWorldLocation(Location);
	return Path->GetDistanceAlongSplineAtSplineInputKey(Key);
}

FVector ASiegeLane::GetPackSlot(int32 Index) const
{
	if (!Path)
	{
		return GetActorLocation();
	}
	// Column along the first stretch of the lane: the pack is born already walking in file, and a
	// long pack reaches further in rather than further back off the map.
	const float Distance = FMath::Min(Index * PackSpacing, Path->GetSplineLength());
	const FVector Along = Path->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
	const FVector Right = Path->GetRightVectorAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World).GetSafeNormal2D();
	return Along + Right * FMath::FRandRange(-PackSideJitter, PackSideJitter);
}

void ASiegeLane::GetOpenLanes(const UWorld* World, TArray<ASiegeLane*>& OutLanes)
{
	OutLanes.Reset();
	if (!World)
	{
		return;
	}
	for (TActorIterator<ASiegeLane> It(World); It; ++It)
	{
		if (IsValid(*It) && It->bLaneOpen)
		{
			OutLanes.Add(*It);
		}
	}
	// Stable order, so logs and "every Nth lane" rules read the same every run.
	OutLanes.Sort([](const ASiegeLane& A, const ASiegeLane& B) { return A.LaneName.LexicalLess(B.LaneName); });
}
