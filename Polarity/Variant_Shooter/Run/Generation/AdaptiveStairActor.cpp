#include "AdaptiveStairActor.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

AAdaptiveStairActor::AAdaptiveStairActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	SetRootComponent(SceneRoot);

	auto ConfigureVisualInstances = [this](UHierarchicalInstancedStaticMeshComponent* Component)
	{
		Component->SetupAttachment(SceneRoot);
		Component->SetMobility(EComponentMobility::Static);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
	};

	StepInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("StepInstances"));
	ConfigureVisualInstances(StepInstances);

	LeftRailInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("LeftRailInstances"));
	ConfigureVisualInstances(LeftRailInstances);

	RightRailInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("RightRailInstances"));
	ConfigureVisualInstances(RightRailInstances);

	auto ConfigureVisualCap = [this](UStaticMeshComponent* Component)
	{
		Component->SetupAttachment(SceneRoot);
		Component->SetMobility(EComponentMobility::Static);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
	};

	TopCapComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TopCapComponent"));
	ConfigureVisualCap(TopCapComponent);

	BottomCapComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BottomCapComponent"));
	ConfigureVisualCap(BottomCapComponent);

	auto ConfigureCollisionBox = [this](UBoxComponent* Component, const bool bAffectsNavigation)
	{
		Component->SetupAttachment(SceneRoot);
		Component->SetMobility(EComponentMobility::Static);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCollisionObjectType(ECC_WorldStatic);
		Component->SetCollisionResponseToAllChannels(ECR_Block);
		Component->SetHiddenInGame(true);
		Component->SetCanEverAffectNavigation(bAffectsNavigation);
	};

	RampCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("RampCollision"));
	ConfigureCollisionBox(RampCollision, true);

	LeftSideCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("LeftSideCollision"));
	ConfigureCollisionBox(LeftSideCollision, false);

	RightSideCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("RightSideCollision"));
	ConfigureCollisionBox(RightSideCollision, false);

#if WITH_EDITORONLY_DATA
	StairPreview = CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("StairPreview"));
	if (StairPreview)
	{
		StairPreview->SetupAttachment(SceneRoot);
		StairPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		StairPreview->SetGenerateOverlapEvents(false);
		StairPreview->SetCanEverAffectNavigation(false);
		StairPreview->ShapeColor = FColor(230, 150, 45);
		StairPreview->SetLineThickness(4.f);
	}

	DirectionArrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("DirectionArrow"));
	if (DirectionArrow)
	{
		DirectionArrow->SetupAttachment(SceneRoot);
		DirectionArrow->ArrowColor = FColor(255, 190, 40);
		DirectionArrow->ArrowSize = 2.f;
	}
#endif
}

void AAdaptiveStairActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RegenerateStairs();
}

void AAdaptiveStairActor::RegenerateStairs()
{
	if (bIsRegenerating || !SceneRoot)
	{
		return;
	}

	TGuardValue<bool> RebuildGuard(bIsRegenerating, true);
	SceneRoot->SetMobility(EComponentMobility::Static);

	// Child Blueprints created before new native components are introduced can deserialize
	// inherited components without the expected attachment. Repair them idempotently.
	auto EnsureAttachedToRoot = [this](USceneComponent* Component)
	{
		if (Component && Component != SceneRoot && Component->GetAttachParent() != SceneRoot)
		{
			Component->AttachToComponent(SceneRoot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
	};
	EnsureAttachedToRoot(StepInstances);
	EnsureAttachedToRoot(LeftRailInstances);
	EnsureAttachedToRoot(RightRailInstances);
	EnsureAttachedToRoot(TopCapComponent);
	EnsureAttachedToRoot(BottomCapComponent);
	EnsureAttachedToRoot(RampCollision);
	EnsureAttachedToRoot(LeftSideCollision);
	EnsureAttachedToRoot(RightSideCollision);
#if WITH_EDITORONLY_DATA
	EnsureAttachedToRoot(StairPreview);
	EnsureAttachedToRoot(DirectionArrow);
#endif

	Width = FMath::Max(Width, 50.f);
	DesiredTreadDepth = FMath::Max(DesiredTreadDepth, 10.f);
	MaxRiserHeight = FMath::Max(MaxRiserHeight, 1.f);
	MinTreadDepth = FMath::Max(MinTreadDepth, 1.f);
	MaxSlopeAngle = FMath::Clamp(MaxSlopeAngle, 1.f, 80.f);
	MaxGeneratedSteps = FMath::Clamp(MaxGeneratedSteps, 1, 1024);
	AuthoredTreadDepth = FMath::Max(AuthoredTreadDepth, 1.f);
	AuthoredRiserHeight = FMath::Max(AuthoredRiserHeight, 1.f);
	AuthoredModuleWidth = FMath::Max(AuthoredModuleWidth, 10.f);
	RampCollisionThickness = FMath::Max(RampCollisionThickness, 1.f);
	SideCollisionHeight = FMath::Max(SideCollisionHeight, 1.f);
	SideCollisionThickness = FMath::Max(SideCollisionThickness, 1.f);

	float Rise = 0.f;
	float HorizontalRun = 0.f;
	float HeadingYawDegrees = 0.f;
	FVector HorizontalDirection = FVector::ForwardVector;

	if (FitMode == EAdaptiveStairFitMode::TargetWorldZ)
	{
		Rise = GetActorLocation().Z - TargetFloorZ;
		HorizontalDirection = FVector::ForwardVector;
		HeadingYawDegrees = 0.f;
	}
	else
	{
		Rise = -BottomPointLocal.Z;
		HorizontalRun = FVector(BottomPointLocal.X, BottomPointLocal.Y, 0.f).Size();
		if (HorizontalRun > KINDA_SMALL_NUMBER)
		{
			HorizontalDirection = FVector(BottomPointLocal.X, BottomPointLocal.Y, 0.f) / HorizontalRun;
			HeadingYawDegrees = FMath::RadiansToDegrees(FMath::Atan2(HorizontalDirection.Y, HorizontalDirection.X));
		}
	}

	if (Rise <= KINDA_SMALL_NUMBER)
	{
		ClearGeneratedGeometry();
		GeneratedValidationMessage = TEXT("Lower connection must be below the stair actor origin.");
		UE_LOG(LogTemp, Warning, TEXT("AdaptiveStairs invalid: %s - %s"), *GetName(), *GeneratedValidationMessage);
		return;
	}

	const int32 RiserDrivenCount = FMath::Max(1, FMath::CeilToInt(Rise / MaxRiserHeight));
	int32 RequestedStepCount = RiserDrivenCount;
	if (FitMode == EAdaptiveStairFitMode::LocalEndpoint)
	{
		if (HorizontalRun <= KINDA_SMALL_NUMBER)
		{
			ClearGeneratedGeometry();
			GeneratedValidationMessage = TEXT("Local endpoint needs a non-zero horizontal run.");
			UE_LOG(LogTemp, Warning, TEXT("AdaptiveStairs invalid: %s - %s"), *GetName(), *GeneratedValidationMessage);
			return;
		}

		const int32 TreadDrivenCount = FMath::Max(1, FMath::RoundToInt(HorizontalRun / DesiredTreadDepth));
		RequestedStepCount = FMath::Max(RiserDrivenCount, TreadDrivenCount);
	}

	GeneratedStepCount = FMath::Clamp(RequestedStepCount, 1, MaxGeneratedSteps);
	GeneratedRiserHeight = Rise / static_cast<float>(GeneratedStepCount);
	if (FitMode == EAdaptiveStairFitMode::TargetWorldZ)
	{
		GeneratedTreadDepth = DesiredTreadDepth;
		HorizontalRun = GeneratedTreadDepth * static_cast<float>(GeneratedStepCount);
	}
	else
	{
		GeneratedTreadDepth = HorizontalRun / static_cast<float>(GeneratedStepCount);
	}

	GeneratedSlopeAngle = FMath::RadiansToDegrees(FMath::Atan2(Rise, HorizontalRun));
	ResolvedBottomPointLocal = HorizontalDirection * HorizontalRun + FVector(0.f, 0.f, -Rise);

	const bool bStepCapRespected = RequestedStepCount <= MaxGeneratedSteps;
	const bool bRiserValid = GeneratedRiserHeight <= MaxRiserHeight + KINDA_SMALL_NUMBER;
	const bool bTreadValid = GeneratedTreadDepth >= MinTreadDepth - KINDA_SMALL_NUMBER;
	const bool bSlopeValid = GeneratedSlopeAngle <= MaxSlopeAngle + KINDA_SMALL_NUMBER;
	const FRotator ActorRotation = GetActorRotation();
	const bool bActorIsLevel = FMath::Abs(ActorRotation.Pitch) < 0.1f && FMath::Abs(ActorRotation.Roll) < 0.1f;
	bGeneratedLayoutWithinLimits = bStepCapRespected && bRiserValid && bTreadValid && bSlopeValid && bActorIsLevel;

	TArray<FString> Warnings;
	if (!bStepCapRespected)
	{
		Warnings.Add(FString::Printf(TEXT("needs %d steps but MaxGeneratedSteps is %d"), RequestedStepCount, MaxGeneratedSteps));
	}
	if (!bRiserValid)
	{
		Warnings.Add(FString::Printf(TEXT("riser %.1f exceeds %.1f cm"), GeneratedRiserHeight, MaxRiserHeight));
	}
	if (!bTreadValid)
	{
		Warnings.Add(FString::Printf(TEXT("tread %.1f is below %.1f cm"), GeneratedTreadDepth, MinTreadDepth));
	}
	if (!bSlopeValid)
	{
		Warnings.Add(FString::Printf(TEXT("slope %.1f exceeds %.1f deg"), GeneratedSlopeAngle, MaxSlopeAngle));
	}
	if (!bActorIsLevel)
	{
		Warnings.Add(TEXT("actor pitch and roll must remain zero"));
	}
	GeneratedValidationMessage = Warnings.IsEmpty() ? TEXT("Layout is valid.") : FString::Join(Warnings, TEXT("; "));

	ConfigureRepeatedInstances(GeneratedStepCount, GeneratedTreadDepth, GeneratedRiserHeight, HeadingYawDegrees);
	ConfigureCap(TopCapComponent, TopCapMesh, FVector::ZeroVector, HeadingYawDegrees, TopCapOffset);
	ConfigureCap(BottomCapComponent, BottomCapMesh, ResolvedBottomPointLocal, HeadingYawDegrees, BottomCapOffset);
	ConfigureCollision(ResolvedBottomPointLocal, HeadingYawDegrees, GeneratedSlopeAngle);

#if WITH_EDITORONLY_DATA
	const FRotator SlopeRotation(-GeneratedSlopeAngle, HeadingYawDegrees, 0.f);
	const float SlopeLength = ResolvedBottomPointLocal.Size();
	if (StairPreview)
	{
		StairPreview->SetBoxExtent(FVector(SlopeLength * 0.5f, Width * 0.5f, 10.f));
		StairPreview->SetRelativeLocation(ResolvedBottomPointLocal * 0.5f);
		StairPreview->SetRelativeRotation(SlopeRotation);
		StairPreview->ShapeColor = bGeneratedLayoutWithinLimits ? FColor(230, 150, 45) : FColor(230, 55, 45);
		StairPreview->SetVisibility(StepSegmentMesh == nullptr || !bGeneratedLayoutWithinLimits);
	}

	if (DirectionArrow)
	{
		DirectionArrow->SetVisibility(true);
		DirectionArrow->SetRelativeLocation(FVector::ZeroVector);
		DirectionArrow->SetRelativeRotation(SlopeRotation);
		DirectionArrow->ArrowLength = FMath::Clamp(SlopeLength * 0.15f, 150.f, 1000.f);
		DirectionArrow->ArrowColor = bGeneratedLayoutWithinLimits ? FColor(255, 190, 40) : FColor(255, 60, 40);
	}
#endif

	UE_LOG(LogTemp, Display,
		TEXT("AdaptiveStairs regenerated: %s Steps=%d Tread=%.1f Riser=%.1f Run=%.1f Rise=%.1f Slope=%.1f Valid=%s Mesh=%s"),
		*GetName(), GeneratedStepCount, GeneratedTreadDepth, GeneratedRiserHeight, HorizontalRun, Rise,
		GeneratedSlopeAngle, bGeneratedLayoutWithinLimits ? TEXT("true") : TEXT("false"), *GetNameSafe(StepSegmentMesh));
	if (!bGeneratedLayoutWithinLimits)
	{
		UE_LOG(LogTemp, Warning, TEXT("AdaptiveStairs validation: %s - %s"), *GetName(), *GeneratedValidationMessage);
	}
}

void AAdaptiveStairActor::ClearGeneratedGeometry()
{
	if (StepInstances)
	{
		StepInstances->ClearInstances();
		StepInstances->SetVisibility(false);
	}
	if (LeftRailInstances)
	{
		LeftRailInstances->ClearInstances();
		LeftRailInstances->SetVisibility(false);
	}
	if (RightRailInstances)
	{
		RightRailInstances->ClearInstances();
		RightRailInstances->SetVisibility(false);
	}

	auto ClearCap = [](UStaticMeshComponent* Component)
	{
		if (Component)
		{
			Component->SetStaticMesh(nullptr);
			Component->SetVisibility(false);
			Component->SetRelativeTransform(FTransform::Identity);
		}
	};
	ClearCap(TopCapComponent);
	ClearCap(BottomCapComponent);

	if (RampCollision)
	{
		RampCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (LeftSideCollision)
	{
		LeftSideCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (RightSideCollision)
	{
		RightSideCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	GeneratedStepCount = 0;
	GeneratedTreadDepth = 0.f;
	GeneratedRiserHeight = 0.f;
	GeneratedSlopeAngle = 0.f;
	ResolvedBottomPointLocal = FVector::ZeroVector;
	bGeneratedLayoutWithinLimits = false;

#if WITH_EDITORONLY_DATA
	if (StairPreview)
	{
		StairPreview->SetVisibility(false);
	}
	if (DirectionArrow)
	{
		DirectionArrow->SetVisibility(false);
	}
#endif
}

void AAdaptiveStairActor::ConfigureRepeatedInstances(
	const int32 StepCount,
	const float TreadDepth,
	const float RiserHeight,
	const float HeadingYawDegrees)
{
	if (!StepInstances || !LeftRailInstances || !RightRailInstances)
	{
		return;
	}

	StepInstances->ClearInstances();
	LeftRailInstances->ClearInstances();
	RightRailInstances->ClearInstances();
	StepInstances->SetStaticMesh(StepSegmentMesh);
	LeftRailInstances->SetStaticMesh(LeftRailSegmentMesh);
	RightRailInstances->SetStaticMesh(RightRailSegmentMesh);

	if (!StepSegmentMesh)
	{
		StepInstances->SetVisibility(false);
		LeftRailInstances->SetVisibility(false);
		RightRailInstances->SetVisibility(false);
		return;
	}

	const float TreadScale = TreadDepth / AuthoredTreadDepth;
	const float RiserScale = RiserHeight / AuthoredRiserHeight;
	const float WidthScale = Width / AuthoredModuleWidth;
	const FRotator HeadingRotation(0.f, HeadingYawDegrees, 0.f);
	const FVector HorizontalDirection = HeadingRotation.RotateVector(FVector::ForwardVector);
	auto AddRepeatedMesh = [
		StepCount, TreadDepth, RiserHeight, TreadScale, RiserScale, WidthScale,
		HeadingRotation, HorizontalDirection](
		UHierarchicalInstancedStaticMeshComponent* Component,
		UStaticMesh* Mesh,
		const FVector& ExtraOffset)
	{
		if (!Component || !Mesh)
		{
			return;
		}

		const FVector RotatedOffset = HeadingRotation.RotateVector(ExtraOffset);
		for (int32 Index = 0; Index < StepCount; ++Index)
		{
			const FVector Location = HorizontalDirection * (Index * TreadDepth)
				+ FVector(0.f, 0.f, -Index * RiserHeight)
				+ RotatedOffset;
			const FVector Scale(TreadScale, WidthScale, RiserScale);
			const FTransform LocalTransform(HeadingRotation, Location, Scale);
			Component->AddInstance(LocalTransform, false);
		}
	};

	AddRepeatedMesh(StepInstances, StepSegmentMesh, StepSegmentOffset);
	AddRepeatedMesh(LeftRailInstances, LeftRailSegmentMesh, LeftRailSegmentOffset);
	AddRepeatedMesh(RightRailInstances, RightRailSegmentMesh, RightRailSegmentOffset);
	StepInstances->SetVisibility(true);
	LeftRailInstances->SetVisibility(LeftRailSegmentMesh != nullptr);
	RightRailInstances->SetVisibility(RightRailSegmentMesh != nullptr);
}

void AAdaptiveStairActor::ConfigureCap(
	UStaticMeshComponent* Component,
	UStaticMesh* Mesh,
	const FVector& ConnectionPoint,
	const float HeadingYawDegrees,
	const FVector& ExtraOffset)
{
	if (!Component)
	{
		return;
	}

	Component->SetStaticMesh(Mesh);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetVisibility(Mesh != nullptr);
	if (!Mesh)
	{
		Component->SetRelativeTransform(FTransform::Identity);
		return;
	}

	const float WidthScale = Width / AuthoredModuleWidth;
	const FRotator HeadingRotation(0.f, HeadingYawDegrees, 0.f);
	const FVector Location = ConnectionPoint + HeadingRotation.RotateVector(ExtraOffset);
	const FTransform LocalTransform(HeadingRotation, Location, FVector(1.f, WidthScale, 1.f));
	Component->SetRelativeTransform(LocalTransform);
}

void AAdaptiveStairActor::ConfigureCollision(
	const FVector& LowerConnection,
	const float HeadingYawDegrees,
	const float SlopeAngleDegrees)
{
	const float SlopeLength = LowerConnection.Size();
	const FRotator SlopeRotation(-SlopeAngleDegrees, HeadingYawDegrees, 0.f);
	const FVector SlopeUp = SlopeRotation.RotateVector(FVector::UpVector);
	const FVector HorizontalRight = FRotator(0.f, HeadingYawDegrees, 0.f).RotateVector(FVector::RightVector);

	if (RampCollision)
	{
		RampCollision->SetCollisionEnabled(bGenerateRampCollision
			? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::NoCollision);
		RampCollision->SetBoxExtent(FVector(SlopeLength * 0.5f, Width * 0.5f, RampCollisionThickness * 0.5f));
		// The top face follows the step-nosing line; the box body remains beneath it.
		RampCollision->SetRelativeLocation(LowerConnection * 0.5f - SlopeUp * (RampCollisionThickness * 0.5f));
		RampCollision->SetRelativeRotation(SlopeRotation);
	}

	auto ConfigureSide = [
		this, SlopeLength, LowerConnection, SlopeRotation, SlopeUp, HorizontalRight](
		UBoxComponent* Component,
		const float SideSign)
	{
		if (!Component)
		{
			return;
		}

		Component->SetCollisionEnabled(bGenerateSideCollision
			? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::NoCollision);
		Component->SetBoxExtent(FVector(SlopeLength * 0.5f, SideCollisionThickness * 0.5f, SideCollisionHeight * 0.5f));
		const FVector SideOffset = HorizontalRight * SideSign * (Width * 0.5f + SideCollisionThickness * 0.5f);
		Component->SetRelativeLocation(LowerConnection * 0.5f + SideOffset + SlopeUp * (SideCollisionHeight * 0.5f));
		Component->SetRelativeRotation(SlopeRotation);
	};

	ConfigureSide(LeftSideCollision, -1.f);
	ConfigureSide(RightSideCollision, 1.f);
}
