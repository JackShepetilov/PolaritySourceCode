// ExtractionRoute.cpp

#include "Variant_Shooter/Map/ExtractionRoute.h"

#include "Variant_Shooter/Map/RunDirectorSubsystem.h"

#include "Components/SphereComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"

// ==================== AExtractionPoint ====================

AExtractionPoint::AExtractionPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	BoardGizmo = CreateDefaultSubobject<USphereComponent>(TEXT("BoardGizmo"));
	SetRootComponent(BoardGizmo);

	BoardGizmo->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BoardGizmo->SetCollisionResponseToAllChannels(ECR_Ignore);
	BoardGizmo->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	BoardGizmo->SetGenerateOverlapEvents(true);
	BoardGizmo->SetHiddenInGame(true);
	BoardGizmo->ShapeColor = FColor(90, 220, 140);
	BoardGizmo->SetSphereRadius(BoardRadius, false);
}

void AExtractionPoint::BeginPlay()
{
	Super::BeginPlay();

	BoardGizmo->SetSphereRadius(BoardRadius, false);
}

// ==================== AExtractionRoute ====================

AExtractionRoute::AExtractionRoute()
{
	PrimaryActorTick.bCanEverTick = false;

	Path = CreateDefaultSubobject<USplineComponent>(TEXT("Path"));
	SetRootComponent(Path);
}

void AExtractionRoute::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (URunDirectorSubsystem* Director = World->GetSubsystem<URunDirectorSubsystem>())
		{
			Director->RegisterExtractionRoute(this);
		}
	}
}

void AExtractionRoute::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HasAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			if (URunDirectorSubsystem* Director = World->GetSubsystem<URunDirectorSubsystem>())
			{
				Director->UnregisterExtractionRoute(this);
			}
		}
	}

	Super::EndPlay(Reason);
}

FVector AExtractionRoute::GetPointAlongRoute(float Alpha) const
{
	if (!Path)
	{
		return GetActorLocation();
	}

	const float Distance = FMath::Clamp(Alpha, 0.0f, 1.0f) * Path->GetSplineLength();
	return Path->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
}

float AExtractionRoute::GetRouteLength() const
{
	return Path ? Path->GetSplineLength() : 0.0f;
}
