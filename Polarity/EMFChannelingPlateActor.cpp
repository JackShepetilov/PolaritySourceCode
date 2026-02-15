// EMFChannelingPlateActor.cpp

#include "EMFChannelingPlateActor.h"
#include "EMF_FieldComponent.h"
#include "EMF_PluginBPLibrary.h"
#include "DrawDebugHelpers.h"

AEMFChannelingPlateActor::AEMFChannelingPlateActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// No collision, no visual
	SetCanBeDamaged(false);

	// Root component — required for SetActorLocation to work
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// Create the field component
	PlateFieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("PlateField"));

	// Configure as FinitePlate
	PlateFieldComponent->bUseOwnerInterface = false;
	PlateFieldComponent->bAutoRegister = false; // We register manually after spawn
	PlateFieldComponent->bSimulatePhysics = false;

	PlateFieldComponent->SourceParams.SourceType = EEMSourceType::FinitePlate;
	PlateFieldComponent->SourceParams.bIsStatic = false; // Moves every frame
	PlateFieldComponent->SourceParams.bShowFieldLines = false;
	PlateFieldComponent->SourceParams.OwnerType = EEMSourceOwnerType::Player;

	// Default plate parameters
	PlateFieldComponent->SourceParams.PlateParams.SurfaceChargeDensity = 1.0f;
	PlateFieldComponent->SourceParams.PlateParams.Normal = FVector(1.0f, 0.0f, 0.0f); // Forward
	PlateFieldComponent->SourceParams.PlateParams.Dimensions = FVector2D(200.0f, 200.0f);
	PlateFieldComponent->SourceParams.PlateParams.bIsInfinite = false;
	PlateFieldComponent->SourceParams.PlateParams.MaxDistance = 0.0f;
	PlateFieldComponent->SourceParams.PlateParams.FalloffDistance = 0.0f;
}

void AEMFChannelingPlateActor::BeginPlay()
{
	Super::BeginPlay();

	// Register with the EMF source registry
	if (PlateFieldComponent)
	{
		PlateFieldComponent->RegisterWithRegistry();
	}
}

void AEMFChannelingPlateActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unregister from registry
	if (PlateFieldComponent)
	{
		PlateFieldComponent->UnregisterFromRegistry();
	}

	Super::EndPlay(EndPlayReason);
}

void AEMFChannelingPlateActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bDrawDebugPlate)
	{
		DrawDebug();
	}
}

void AEMFChannelingPlateActor::SetPlateChargeDensity(float Density)
{
	if (PlateFieldComponent)
	{
		FEMSourceDescription Desc = PlateFieldComponent->GetSourceDescription();
		Desc.PlateParams.SurfaceChargeDensity = Density;
		PlateFieldComponent->SetSourceDescription(Desc);
	}
}

float AEMFChannelingPlateActor::GetPlateChargeDensity() const
{
	if (PlateFieldComponent)
	{
		return PlateFieldComponent->GetSourceDescription().PlateParams.SurfaceChargeDensity;
	}
	return 0.0f;
}

void AEMFChannelingPlateActor::SetPlateDimensions(FVector2D Dimensions)
{
	CachedDimensions = Dimensions;

	if (PlateFieldComponent)
	{
		FEMSourceDescription Desc = PlateFieldComponent->GetSourceDescription();
		Desc.PlateParams.Dimensions = Dimensions;
		PlateFieldComponent->SetSourceDescription(Desc);
	}
}

void AEMFChannelingPlateActor::UpdateTransformFromCamera(const FVector& CameraLocation, const FRotator& CameraRotation, const FVector& LocalOffset)
{
	// Convert local offset to world space using camera rotation
	FVector WorldOffset = CameraRotation.RotateVector(LocalOffset);
	FVector PlatePosition = CameraLocation + WorldOffset;

	// Move the actor — the registry uses GetActorLocation() for position,
	// so this is the authoritative way to set the source position
	SetActorLocationAndRotation(PlatePosition, CameraRotation);

	// Update the plate normal direction to match camera forward
	CachedPlateNormal = CameraRotation.RotateVector(FVector(1.0f, 0.0f, 0.0f));

	if (PlateFieldComponent)
	{
		FEMSourceDescription Desc = PlateFieldComponent->GetSourceDescription();
		Desc.PlateParams.Normal = CachedPlateNormal;
		PlateFieldComponent->SetSourceDescription(Desc);
	}
}

void AEMFChannelingPlateActor::DrawDebug() const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector Position = GetActorLocation();
	FRotator Rotation = GetActorRotation();

	// Get charge density for color
	float Density = GetPlateChargeDensity();
	FColor PlateColor = (Density > 0.0f) ? FColor::Red : FColor::Blue;

	// Draw box representing the plate (thin in forward direction)
	FVector BoxExtent(2.0f, CachedDimensions.X * 0.5f, CachedDimensions.Y * 0.5f);
	DrawDebugBox(World, Position, BoxExtent, Rotation.Quaternion(), PlateColor, false, 0.0f, 0, 2.0f);

	// Draw normal arrow (forward direction)
	FVector Forward = Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f));
	DrawDebugDirectionalArrow(World, Position, Position + Forward * 100.0f, 15.0f, FColor::Green, false, 0.0f, 0, 2.0f);

	// Draw charge density text
	FString DensityText = FString::Printf(TEXT("σ=%.2f μC/m²"), Density);
	DrawDebugString(World, Position + FVector(0.0f, 0.0f, BoxExtent.Z + 10.0f), DensityText, nullptr, PlateColor, 0.0f, true);
#endif
}
