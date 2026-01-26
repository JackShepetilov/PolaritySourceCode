// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "CheckpointActor.h"
#include "CheckpointSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Polarity/Variant_Shooter/ShooterCharacter.h"

ACheckpointActor::ACheckpointActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// Root component
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Trigger box
	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetupAttachment(Root);
	TriggerBox->SetBoxExtent(FVector(50.0f, 200.0f, 200.0f));
	TriggerBox->SetCollisionProfileName(TEXT("Trigger"));
	TriggerBox->SetGenerateOverlapEvents(true);

	// Visual mesh - placeholder plane/cube
	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(Root);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetCastShadow(false);

	// Will be set up in Blueprint or via default mesh
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane"));
	if (PlaneMesh.Succeeded())
	{
		VisualMesh->SetStaticMesh(PlaneMesh.Object);
		VisualMesh->SetRelativeScale3D(FVector(4.0f, 4.0f, 1.0f));
		VisualMesh->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	}

	// Checkpoint text
	CheckpointText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("CheckpointText"));
	CheckpointText->SetupAttachment(Root);
	CheckpointText->SetText(FText::FromString(TEXT("CHECKPOINT")));
	CheckpointText->SetTextRenderColor(FColor::White);
	CheckpointText->SetHorizontalAlignment(EHTA_Center);
	CheckpointText->SetVerticalAlignment(EVRTA_TextCenter);
	CheckpointText->SetWorldSize(50.0f);
	CheckpointText->SetRelativeLocation(FVector(10.0f, 0.0f, 0.0f));

	// Spawn point (where player will respawn)
	SpawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("SpawnPoint"));
	SpawnPoint->SetupAttachment(Root);
	SpawnPoint->SetRelativeLocation(FVector(-200.0f, 0.0f, 0.0f)); // In front of checkpoint

	// Generate unique ID
	CheckpointID = FGuid::NewGuid();
}

void ACheckpointActor::BeginPlay()
{
	Super::BeginPlay();

	// Register with subsystem
	if (UCheckpointSubsystem* Subsystem = GetWorld()->GetSubsystem<UCheckpointSubsystem>())
	{
		Subsystem->RegisterCheckpoint(this);
	}

	// Bind overlap
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ACheckpointActor::OnTriggerOverlap);

	// Setup dynamic material
	if (VisualMesh && VisualMesh->GetStaticMesh())
	{
		DynamicMaterial = VisualMesh->CreateAndSetMaterialInstanceDynamic(0);
		UpdateVisualState();
	}
}

void ACheckpointActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unregister from subsystem
	if (UCheckpointSubsystem* Subsystem = GetWorld()->GetSubsystem<UCheckpointSubsystem>())
	{
		Subsystem->UnregisterCheckpoint(this);
	}

	Super::EndPlay(EndPlayReason);
}

FTransform ACheckpointActor::GetSpawnTransform() const
{
	if (SpawnPoint)
	{
		return SpawnPoint->GetComponentTransform();
	}
	return GetActorTransform();
}

void ACheckpointActor::OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Only activate for player characters
	AShooterCharacter* Character = Cast<AShooterCharacter>(OtherActor);
	if (!Character)
	{
		return;
	}

	// Check if already activated and can't reactivate
	if (bWasActivated && !bCanReactivate)
	{
		return;
	}

	ActivateCheckpoint(Character);
}

void ACheckpointActor::ActivateCheckpoint(AShooterCharacter* Character)
{
	if (!IsValid(Character))
	{
		return;
	}

	UCheckpointSubsystem* Subsystem = GetWorld()->GetSubsystem<UCheckpointSubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("CheckpointActor: No CheckpointSubsystem found"));
		return;
	}

	// Try to activate through subsystem
	if (!Subsystem->ActivateCheckpoint(this, Character))
	{
		return;
	}

	bWasActivated = true;

	// Play feedback
	if (ActivationSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ActivationSound, GetActorLocation());
	}

	if (ActivationVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ActivationVFX, GetActorLocation());
	}

	// Update visuals
	UpdateVisualState();

	// Blueprint event
	BP_OnCheckpointActivated(Character);
}

void ACheckpointActor::UpdateVisualState()
{
	if (bWasActivated)
	{
		if (bHideAfterActivation)
		{
			VisualMesh->SetVisibility(false);
			CheckpointText->SetVisibility(false);
		}
		else if (DynamicMaterial)
		{
			DynamicMaterial->SetVectorParameterValue(TEXT("Color"), ActiveColor);
		}
	}
	else
	{
		if (DynamicMaterial)
		{
			DynamicMaterial->SetVectorParameterValue(TEXT("Color"), InactiveColor);
		}
	}
}
