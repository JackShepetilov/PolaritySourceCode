// ExtractionRoute.cpp

#include "Variant_Shooter/Map/ExtractionRoute.h"

#include "Variant_Shooter/Map/RunDirectorSubsystem.h"
#include "Coop/CoopPlayers.h"

#include "Components/SphereComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

// ==================== AExtractionPoint ====================

AExtractionPoint::AExtractionPoint()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;

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

float AExtractionPoint::GetBoardProgress() const
{
	return FMath::Clamp(BoardedSeconds / FMath::Max(BoardSeconds, 0.01f), 0.0f, 1.0f);
}

void AExtractionPoint::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

	const UWorld* World = GetWorld();
	URunDirectorSubsystem* Director = World ? World->GetSubsystem<URunDirectorSubsystem>() : nullptr;
	if (!Director)
	{
		return;
	}

	// A door only opens for the route the team was actually given. Three exits are placed and one is
	// drawn; walking to a different one is walking to a locked door, which is the point of drawing.
	const AExtractionRoute* Route = Director->GetAnnouncedRoute();
	if (Director->GetPhase() != ERunPhase::Extraction || !Route || Route->Exit != this)
	{
		BoardedSeconds = 0.0f;
		return;
	}

	TArray<AActor*> Overlapping;
	BoardGizmo->GetOverlappingActors(Overlapping, APawn::StaticClass());

	bool bAnybodyAboard = false;
	for (const AActor* Actor : Overlapping)
	{
		if (CoopPlayers::IsPlayer(Actor))
		{
			bAnybodyAboard = true;
			break;
		}
	}

	if (!bAnybodyAboard)
	{
		BoardedSeconds = 0.0f;
		return;
	}

	BoardedSeconds += DeltaSeconds;

	if (BoardedSeconds >= BoardSeconds)
	{
		UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Extracted through %s"), *ExitTag.ToString());
		Director->EndRun(true);
	}
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
