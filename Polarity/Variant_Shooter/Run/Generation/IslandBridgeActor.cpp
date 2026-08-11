#include "IslandBridgeActor.h"
#include "Arena/ArenaManager.h"
#include "RunSubsystem.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

AIslandBridgeActor::AIslandBridgeActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	GeneratedMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GeneratedMesh"));
	GeneratedMesh->SetupAttachment(SceneRoot);
	GeneratedMesh->SetMobility(EComponentMobility::Static);

	DeckInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("DeckInstances"));
	DeckInstances->SetupAttachment(SceneRoot);
	DeckInstances->SetMobility(EComponentMobility::Static);
	DeckInstances->SetGenerateOverlapEvents(false);

	LeftRailInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("LeftRailInstances"));
	LeftRailInstances->SetupAttachment(SceneRoot);
	LeftRailInstances->SetMobility(EComponentMobility::Static);
	LeftRailInstances->SetGenerateOverlapEvents(false);

	RightRailInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("RightRailInstances"));
	RightRailInstances->SetupAttachment(SceneRoot);
	RightRailInstances->SetMobility(EComponentMobility::Static);
	RightRailInstances->SetGenerateOverlapEvents(false);

	StartCapComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StartCapComponent"));
	StartCapComponent->SetupAttachment(SceneRoot);
	StartCapComponent->SetMobility(EComponentMobility::Static);
	StartCapComponent->SetGenerateOverlapEvents(false);

	EndCapComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EndCapComponent"));
	EndCapComponent->SetupAttachment(SceneRoot);
	EndCapComponent->SetMobility(EComponentMobility::Static);
	EndCapComponent->SetGenerateOverlapEvents(false);

	ProgressionBlocker = CreateDefaultSubobject<UBoxComponent>(TEXT("ProgressionBlocker"));
	ProgressionBlocker->SetupAttachment(SceneRoot);
	ProgressionBlocker->SetMobility(EComponentMobility::Static);
	ProgressionBlocker->SetCollisionObjectType(ECC_WorldStatic);
	ProgressionBlocker->SetCollisionResponseToAllChannels(ECR_Ignore);
	ProgressionBlocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	ProgressionBlocker->SetGenerateOverlapEvents(false);
	ProgressionBlocker->ShapeColor = FColor::Red;

#if WITH_EDITORONLY_DATA
	BridgePreview = CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("BridgePreview"));
	if (BridgePreview)
	{
		BridgePreview->SetupAttachment(SceneRoot);
		BridgePreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BridgePreview->SetGenerateOverlapEvents(false);
		BridgePreview->ShapeColor = FColor(230, 150, 45);
		BridgePreview->SetLineThickness(4.f);
	}

	ForwardArrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("ForwardArrow"));
	if (ForwardArrow)
	{
		ForwardArrow->SetupAttachment(SceneRoot);
		ForwardArrow->ArrowColor = FColor(255, 190, 40);
		ForwardArrow->ArrowSize = 2.f;
	}
#endif
}


void AIslandBridgeActor::BeginPlay()
{
	Super::BeginPlay();
	SetBridgeLocked(bStartsLocked && UnlockAfterAnchor != nullptr);
}

void AIslandBridgeActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AArenaManager* ArenaManager = BoundArenaManager.Get())
	{
		ArenaManager->OnArenaCleared.RemoveDynamic(this, &AIslandBridgeActor::HandleSourceArenaCleared);
	}
	BoundArenaManager.Reset();
	Super::EndPlay(EndPlayReason);
}

void AIslandBridgeActor::BindUnlockSource(AArenaManager* ArenaManager, const int32 ArenaIndex)
{
	if (AArenaManager* PreviousManager = BoundArenaManager.Get())
	{
		PreviousManager->OnArenaCleared.RemoveDynamic(this, &AIslandBridgeActor::HandleSourceArenaCleared);
	}
	BoundArenaManager = ArenaManager;
	BoundArenaIndex = ArenaIndex;

	if (!bStartsLocked)
	{
		SetBridgeLocked(false);
		return;
	}

	if (URunSubsystem* Run = GetGameInstance() ? GetGameInstance()->GetSubsystem<URunSubsystem>() : nullptr)
	{
		if (Run->IsArenaCleared(ArenaIndex))
		{
			SetBridgeLocked(false);
			return;
		}
	}

	if (ArenaManager && ArenaManager->CurrentState == EArenaState::Completed)
	{
		SetBridgeLocked(false);
		if (URunSubsystem* Run = GetGameInstance() ? GetGameInstance()->GetSubsystem<URunSubsystem>() : nullptr)
		{
			Run->ClearArena(ArenaIndex);
		}
		return;
	}

	SetBridgeLocked(true);
	if (ArenaManager)
	{
		ArenaManager->OnArenaCleared.AddUniqueDynamic(this, &AIslandBridgeActor::HandleSourceArenaCleared);
	}
}

void AIslandBridgeActor::SetBridgeLocked(const bool bLocked)
{
	const bool bChanged = bBridgeLocked != bLocked;
	bBridgeLocked = bLocked;
	if (ProgressionBlocker)
	{
		ProgressionBlocker->SetCollisionEnabled(bLocked ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
	if (bChanged)
	{
		OnBridgeLockStateChanged(bLocked);
		UE_LOG(LogTemp, Log, TEXT("[BIOME_BRIDGE] bridge=%s locked=%d arenaIndex=%d"), *GetActorNameOrLabel(), bLocked ? 1 : 0, BoundArenaIndex);
	}
}

void AIslandBridgeActor::HandleSourceArenaCleared()
{
	SetBridgeLocked(false);
	if (URunSubsystem* Run = GetGameInstance() ? GetGameInstance()->GetSubsystem<URunSubsystem>() : nullptr)
	{
		Run->ClearArena(BoundArenaIndex);
	}
}
void AIslandBridgeActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RegenerateBridge();
}

void AIslandBridgeActor::RegenerateBridge()
{
	if (bIsRegenerating || !GeneratedMesh)
	{
		return;
	}

	TGuardValue<bool> RebuildGuard(bIsRegenerating, true);

	// BP_IslandBridge existed before the modular default subobjects were added.
	// Recompiled child Blueprints can deserialize those new inherited components
	// without the constructor-time SetupAttachment relationship. Repair it here
	// so modular geometry always follows the actor transform instead of staying at world origin.
	auto EnsureAttachedToRoot = [this](USceneComponent* Component)
	{
		if (Component && Component != SceneRoot && Component->GetAttachParent() != SceneRoot)
		{
			Component->AttachToComponent(SceneRoot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
	};
	EnsureAttachedToRoot(GeneratedMesh);
	EnsureAttachedToRoot(DeckInstances);
	EnsureAttachedToRoot(LeftRailInstances);
	EnsureAttachedToRoot(RightRailInstances);
	EnsureAttachedToRoot(StartCapComponent);
	EnsureAttachedToRoot(EndCapComponent);
	EnsureAttachedToRoot(ProgressionBlocker);
#if WITH_EDITORONLY_DATA
	EnsureAttachedToRoot(BridgePreview);
	EnsureAttachedToRoot(ForwardArrow);
#endif

	Length = FMath::Max(Length, 100.f);
	Width = FMath::Max(Width, 50.f);
	Height = FMath::Max(Height, 10.f);
	SegmentLength = FMath::Max(SegmentLength, 10.f);
	AuthoredModuleWidth = FMath::Max(AuthoredModuleWidth, 10.f);
	BlockerLengthPadding = FMath::Max(BlockerLengthPadding, 0.f);
	BlockerHeight = FMath::Max(BlockerHeight, 100.f);
	BlockerWidthPadding = FMath::Max(BlockerWidthPadding, 0.f);

	const bool bHasModularDeck = DeckSegmentMesh != nullptr;
	ConfigureModularInstances();
	ConfigureCap(StartCapComponent, StartCapMesh, false, StartCapOffset);
	ConfigureCap(EndCapComponent, EndCapMesh, true, EndCapOffset);

	ConfigureProgressionBlocker();
	GeneratedMesh->SetStaticMesh(BridgeStaticMesh);
	GeneratedMesh->SetCollisionEnabled(!bHasModularDeck && bGenerateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	GeneratedMesh->SetGenerateOverlapEvents(false);

	if (!bHasModularDeck && BridgeStaticMesh)
	{
		const FBox Bounds = BridgeStaticMesh->GetBoundingBox();
		const FVector SourceSize = Bounds.GetSize().ComponentMax(FVector(1.f));
		const FVector MeshScale(
			Length / SourceSize.X,
			Width / SourceSize.Y,
			bScaleMeshHeight ? Height / SourceSize.Z : 1.f);
		const FVector MeshLocation(
			-Bounds.Min.X * MeshScale.X,
			-Bounds.GetCenter().Y * MeshScale.Y,
			-Bounds.Min.Z * MeshScale.Z);

		GeneratedMesh->SetRelativeScale3D(MeshScale);
		GeneratedMesh->SetRelativeLocation(MeshLocation);
		GeneratedMesh->SetVisibility(true);
	}
	else
	{
		GeneratedMesh->SetRelativeTransform(FTransform::Identity);
		GeneratedMesh->SetVisibility(false);
	}

#if WITH_EDITORONLY_DATA
	if (BridgePreview)
	{
		BridgePreview->SetBoxExtent(FVector(Length * 0.5f, Width * 0.5f, Height * 0.5f));
		BridgePreview->SetRelativeLocation(FVector(Length * 0.5f, 0.f, Height * 0.5f));
		BridgePreview->SetVisibility(!bHasModularDeck && BridgeStaticMesh == nullptr);
	}

	if (ForwardArrow)
	{
		ForwardArrow->SetRelativeLocation(FVector(Length, 0.f, Height));
		ForwardArrow->ArrowLength = FMath::Clamp(Length * 0.1f, 150.f, 1000.f);
	}
#endif

	UE_LOG(LogTemp, Display,
		TEXT("IslandBridge regenerated: %s Length=%.0f Width=%.0f Segments=%d Modular=%s Fallback=%s"),
		*GetName(), Length, Width, DeckInstances ? DeckInstances->GetInstanceCount() : 0,
		bHasModularDeck ? TEXT("true") : TEXT("false"), *GetNameSafe(BridgeStaticMesh));
}


void AIslandBridgeActor::ConfigureProgressionBlocker()
{
	if (!ProgressionBlocker)
	{
		return;
	}
	const FVector Extent(Length * 0.5f + BlockerLengthPadding, Width * 0.5f + BlockerWidthPadding, BlockerHeight * 0.5f);
	const FVector Location(Length * 0.5f, 0.f, BlockerVerticalOffset + BlockerHeight * 0.5f);
	const FTransform LocalTransform(FRotator::ZeroRotator, Location);
	ProgressionBlocker->SetBoxExtent(Extent);
	ProgressionBlocker->SetWorldTransform(LocalTransform * GetActorTransform());
	if (!HasActorBegunPlay())
	{
		ProgressionBlocker->SetCollisionEnabled(bStartsLocked && UnlockAfterAnchor != nullptr ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
}
void AIslandBridgeActor::ConfigureModularInstances()
{
	if (!DeckInstances || !LeftRailInstances || !RightRailInstances)
	{
		return;
	}

	DeckInstances->ClearInstances();
	LeftRailInstances->ClearInstances();
	RightRailInstances->ClearInstances();
	DeckInstances->SetStaticMesh(DeckSegmentMesh);
	LeftRailInstances->SetStaticMesh(LeftRailSegmentMesh);
	RightRailInstances->SetStaticMesh(RightRailSegmentMesh);

	const ECollisionEnabled::Type DeckCollision = bGenerateCollision
		? ECollisionEnabled::QueryAndPhysics
		: ECollisionEnabled::NoCollision;
	DeckInstances->SetCollisionEnabled(DeckCollision);
	LeftRailInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RightRailInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (!DeckSegmentMesh)
	{
		DeckInstances->SetVisibility(false);
		LeftRailInstances->SetVisibility(false);
		RightRailInstances->SetVisibility(false);
		return;
	}

	const int32 SegmentCount = FMath::Max(1, FMath::RoundToInt(Length / SegmentLength));
	const float ActualStep = Length / static_cast<float>(SegmentCount);
	const float WidthScale = Width / AuthoredModuleWidth;
	const FTransform ActorTransform = GetActorTransform();

	auto AddRepeatedMesh = [SegmentCount, ActualStep, WidthScale, ActorTransform, AuthoredStep = SegmentLength](
		UHierarchicalInstancedStaticMeshComponent* Component,
		UStaticMesh* Mesh,
		const FVector& ExtraOffset)
	{
		if (!Component || !Mesh)
		{
			return;
		}

		// Authored modules intentionally overlap their 200 cm repeat boundary
		// (deck bounds are 204 cm, rail bounds are 240 cm). Scale by the declared
		// repeat step, never by mesh bounds, so those overlaps remain intact.
		const float LengthScale = ActualStep / AuthoredStep;
		for (int32 Index = 0; Index < SegmentCount; ++Index)
		{
			const FVector Scale(LengthScale, WidthScale, 1.f);
			const FVector Location(
				Index * ActualStep + ExtraOffset.X,
				ExtraOffset.Y,
				ExtraOffset.Z);
			const FTransform LocalTransform(FRotator::ZeroRotator, Location, Scale);
			Component->AddInstance(LocalTransform * ActorTransform, true);
		}
	};

	AddRepeatedMesh(DeckInstances, DeckSegmentMesh, DeckSegmentOffset);
	AddRepeatedMesh(LeftRailInstances, LeftRailSegmentMesh, LeftRailSegmentOffset);
	AddRepeatedMesh(RightRailInstances, RightRailSegmentMesh, RightRailSegmentOffset);
	DeckInstances->SetVisibility(true);
	LeftRailInstances->SetVisibility(LeftRailSegmentMesh != nullptr);
	RightRailInstances->SetVisibility(RightRailSegmentMesh != nullptr);
}

void AIslandBridgeActor::ConfigureCap(
	UStaticMeshComponent* Component,
	UStaticMesh* Mesh,
	const bool bAlignToEnd,
	const FVector& ExtraOffset)
{
	if (!Component)
	{
		return;
	}

	Component->SetStaticMesh(Mesh);
	Component->SetCollisionEnabled(Mesh && bGenerateCollision
		? ECollisionEnabled::QueryAndPhysics
		: ECollisionEnabled::NoCollision);
	Component->SetVisibility(Mesh != nullptr);
	if (!Mesh)
	{
		Component->SetRelativeTransform(FTransform::Identity);
		return;
	}

	const float WidthScale = Width / AuthoredModuleWidth;
	// Both cap meshes are authored with their pivot at the connection plane.
	const float AlignedX = bAlignToEnd ? Length : 0.f;
	const FTransform LocalTransform(
		FRotator::ZeroRotator,
		FVector(AlignedX, 0.f, 0.f) + ExtraOffset,
		FVector(1.f, WidthScale, 1.f));
	Component->SetWorldTransform(LocalTransform * GetActorTransform());
}
