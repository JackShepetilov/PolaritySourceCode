// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "UpgradePickup.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "ShooterCharacter.h"
#include "UpgradeDefinition.h"
#include "UpgradeManagerComponent.h"
#include "UpgradeTooltipWidget.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

AUpgradePickup::AUpgradePickup()
{
	PrimaryActorTick.bCanEverTick = true;

	// Pickup collision
	PickupCollision = CreateDefaultSubobject<USphereComponent>(TEXT("PickupCollision"));
	SetRootComponent(PickupCollision);
	PickupCollision->SetSphereRadius(100.0f);
	PickupCollision->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	PickupCollision->SetGenerateOverlapEvents(true);

	// Visual mesh
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(PickupCollision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Tooltip trigger sphere (larger radius)
	TooltipTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("TooltipTrigger"));
	TooltipTrigger->SetupAttachment(PickupCollision);
	TooltipTrigger->SetSphereRadius(400.0f);
	TooltipTrigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	TooltipTrigger->SetGenerateOverlapEvents(true);
	TooltipTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	TooltipTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// Tooltip widget component (world-space, hidden by default)
	TooltipWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("TooltipWidget"));
	TooltipWidgetComponent->SetupAttachment(PickupCollision);
	TooltipWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	TooltipWidgetComponent->SetDrawAtDesiredSize(true);
	TooltipWidgetComponent->SetVisibility(false);
}

void AUpgradePickup::BeginPlay()
{
	Super::BeginPlay();

	// Sync collision radii
	PickupCollision->SetSphereRadius(PickupRadius);
	TooltipTrigger->SetSphereRadius(TooltipRadius);

	// Position tooltip above the mesh
	TooltipWidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, TooltipHeight));

	// Bind overlaps
	PickupCollision->OnComponentBeginOverlap.AddDynamic(this, &AUpgradePickup::OnPickupOverlap);
	TooltipTrigger->OnComponentBeginOverlap.AddDynamic(this, &AUpgradePickup::OnTooltipBeginOverlap);
	TooltipTrigger->OnComponentEndOverlap.AddDynamic(this, &AUpgradePickup::OnTooltipEndOverlap);

	// Store initial mesh Z for bob effect
	if (Mesh)
	{
		InitialMeshZ = Mesh->GetRelativeLocation().Z;
	}

	// Spawn idle VFX
	if (IdleVFX)
	{
		IdleVFXComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			IdleVFX, PickupCollision, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset,
			true);
	}

	// Initialize tooltip widget from definition
	if (TooltipWidgetClass && UpgradeDefinition)
	{
		TooltipWidgetComponent->SetWidgetClass(TooltipWidgetClass);
		TooltipWidgetComponent->InitWidget();

		if (UUpgradeTooltipWidget* Tooltip = Cast<UUpgradeTooltipWidget>(TooltipWidgetComponent->GetWidget()))
		{
			Tooltip->InitFromDefinition(UpgradeDefinition);
		}
	}
}

void AUpgradePickup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!Mesh)
	{
		return;
	}

	// Rotate mesh
	if (RotationSpeed > 0.0f)
	{
		Mesh->AddLocalRotation(FRotator(0.0f, RotationSpeed * DeltaTime, 0.0f));
	}

	// Bob mesh up and down
	if (BobAmplitude > 0.0f)
	{
		BobTime += DeltaTime;
		const float BobOffset = FMath::Sin(BobTime * BobFrequency * 2.0f * UE_PI) * BobAmplitude;
		FVector MeshLocation = Mesh->GetRelativeLocation();
		MeshLocation.Z = InitialMeshZ + BobOffset;
		Mesh->SetRelativeLocation(MeshLocation);
	}
}

void AUpgradePickup::OnPickupOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor);
	if (!Player || Player->IsDead())
	{
		return;
	}

	if (!UpgradeDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradePickup: No UpgradeDefinition set on '%s'"), *GetName());
		return;
	}

	UUpgradeManagerComponent* UpgradeMgr = Player->GetUpgradeManager();
	if (!UpgradeMgr)
	{
		return;
	}

	// Check if player already has this upgrade
	if (UpgradeMgr->HasUpgrade(UpgradeDefinition->UpgradeTag))
	{
		BP_OnUpgradeAlreadyOwned(Player);
		return;
	}

	// Grant the upgrade
	if (UpgradeMgr->GrantUpgrade(UpgradeDefinition))
	{
		// Effects
		if (PickupSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, PickupSound, GetActorLocation());
		}

		if (PickupVFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				GetWorld(), PickupVFX, GetActorLocation(),
				FRotator::ZeroRotator, FVector::OneVector,
				true, true, ENCPoolMethod::None);
		}

		BP_OnUpgradePickedUp(Player);

		Destroy();
	}
}

void AUpgradePickup::OnTooltipBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor);
	if (!Player || Player->IsDead())
	{
		return;
	}

	TooltipWidgetComponent->SetVisibility(true);

	if (UUpgradeTooltipWidget* Tooltip = Cast<UUpgradeTooltipWidget>(TooltipWidgetComponent->GetWidget()))
	{
		Tooltip->BP_OnTooltipShow();
	}
}

void AUpgradePickup::OnTooltipEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor);
	if (!Player)
	{
		return;
	}

	TooltipWidgetComponent->SetVisibility(false);

	if (UUpgradeTooltipWidget* Tooltip = Cast<UUpgradeTooltipWidget>(TooltipWidgetComponent->GetWidget()))
	{
		Tooltip->BP_OnTooltipHide();
	}
}
