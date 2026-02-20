// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "UpgradePickup.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ShooterCharacter.h"
#include "UpgradeDefinition.h"
#include "UpgradeManagerComponent.h"
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
}

void AUpgradePickup::BeginPlay()
{
	Super::BeginPlay();

	// Sync collision radius
	PickupCollision->SetSphereRadius(PickupRadius);

	// Bind overlap
	PickupCollision->OnComponentBeginOverlap.AddDynamic(this, &AUpgradePickup::OnPickupOverlap);

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
