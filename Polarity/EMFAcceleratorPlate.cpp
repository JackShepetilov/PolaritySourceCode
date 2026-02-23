// EMFAcceleratorPlate.cpp
// Placeable accelerator plate with overridden capture behavior

#include "EMFAcceleratorPlate.h"
#include "EMF_FieldComponent.h"
#include "EMF_PluginBPLibrary.h"

AEMFAcceleratorPlate::AEMFAcceleratorPlate()
{
	PrimaryActorTick.bCanEverTick = false;

	// Root scene component (user adds meshes in Blueprint)
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// EMF field component configured as AcceleratorPlate
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("FieldComponent"));
	FieldComponent->bUseOwnerInterface = false;
	FieldComponent->bAutoRegister = true;
	FieldComponent->bSimulatePhysics = false;

	FieldComponent->SourceParams.SourceType = EEMSourceType::AcceleratorPlate;
	FieldComponent->SourceParams.bIsStatic = true;
	FieldComponent->SourceParams.bShowFieldLines = false;
	FieldComponent->SourceParams.OwnerType = EEMSourceOwnerType::Environment;

	// Default plate parameters
	FieldComponent->SourceParams.PlateParams.SurfaceChargeDensity = 1.0f;
	FieldComponent->SourceParams.PlateParams.Normal = FVector(0.0f, 0.0f, 1.0f); // Up
	FieldComponent->SourceParams.PlateParams.Dimensions = FVector2D(200.0f, 200.0f);
	FieldComponent->SourceParams.PlateParams.bIsInfinite = false;
	FieldComponent->SourceParams.PlateParams.MaxDistance = 0.0f;
	FieldComponent->SourceParams.PlateParams.FalloffDistance = 0.0f;
}

void AEMFAcceleratorPlate::BeginPlay()
{
	Super::BeginPlay();

	// Apply designer-configured values to the field component
	if (FieldComponent)
	{
		FieldComponent->SourceParams.PlateParams.SurfaceChargeDensity = SurfaceChargeDensity;
		FieldComponent->SourceParams.PlateParams.Dimensions = PlateDimensions;
	}
}

void AEMFAcceleratorPlate::StartCapture()
{
	bIsCaptured = true;
}

void AEMFAcceleratorPlate::StopCapture()
{
	bIsCaptured = false;
}

void AEMFAcceleratorPlate::UpdateHoldPosition(const FVector& CameraLoc, const FRotator& CameraRot)
{
	if (!bIsCaptured)
	{
		return;
	}

	// Transform HoldOffset from camera-local space to world space
	const FVector WorldOffset = CameraRot.RotateVector(HoldOffset);
	const FVector TargetLocation = CameraLoc + WorldOffset;

	SetActorLocation(TargetLocation);
}
