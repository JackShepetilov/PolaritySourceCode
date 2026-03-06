// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "RewardContainer.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Field/FieldSystemObjects.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"

ARewardContainer::ARewardContainer()
{
	PrimaryActorTick.bCanEverTick = false;

	// Static mesh — visible in editor, no physics (floats in air where placed)
	ContainerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ContainerMesh"));
	SetRootComponent(ContainerMesh);
	ContainerMesh->SetSimulatePhysics(false);
	ContainerMesh->SetEnableGravity(false);
	ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Impact probe — kept for header compatibility, not used in current flow
	ImpactProbe = CreateDefaultSubobject<USphereComponent>(TEXT("ImpactProbe"));
	ImpactProbe->SetupAttachment(RootComponent);
	ImpactProbe->SetSphereRadius(30.0f);
	ImpactProbe->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ImpactProbe->SetVisibility(false);
	ImpactProbe->SetHiddenInGame(true);
}

// ==================== Lifecycle ====================

void ARewardContainer::BeginPlay()
{
	Super::BeginPlay();
}

void ARewardContainer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(GibFreezeHandle);
	GetWorldTimerManager().ClearTimer(ScatterImpulseHandle);
	Super::EndPlay(EndPlayReason);
}

// ==================== Activation ====================

void ARewardContainer::Activate()
{
	if (bActivated)
	{
		return;
	}
	bActivated = true;

	UE_LOG(LogTemp, Log, TEXT("RewardContainer %s: Activated — exploding in place"), *GetName());

	// Hide the static mesh preview
	ContainerMesh->SetVisibility(false);

	// Spawn GC at mesh location, immediately break it
	SpawnFallingGC();

	// Notify Blueprints — GC visible, physics active, mesh hidden
	OnContainerActivated.Broadcast();
}

// ==================== GC Spawning + Immediate Break ====================

void ARewardContainer::SpawnFallingGC()
{
	if (!ContainerGC || !GetWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("RewardContainer %s: Missing ContainerGC or World"), *GetName());
		return;
	}

	const FTransform MeshTransform = ContainerMesh->GetComponentTransform();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	SpawnedGCActor = GetWorld()->SpawnActor<AGeometryCollectionActor>(
		MeshTransform.GetLocation(), MeshTransform.GetRotation().Rotator(), SpawnParams);

	if (!SpawnedGCActor)
	{
		return;
	}

	UGeometryCollectionComponent* GCComp = SpawnedGCActor->GetGeometryCollectionComponent();
	if (!GCComp)
	{
		SpawnedGCActor->Destroy();
		SpawnedGCActor = nullptr;
		return;
	}

	// Match mesh scale
	SpawnedGCActor->SetActorScale3D(MeshTransform.GetScale3D());

	// Assign GC asset
	GCComp->SetRestCollection(ContainerGC);

	// Copy materials from ContainerMesh
	const int32 NumMats = ContainerMesh->GetNumMaterials();
	for (int32 i = 0; i < NumMats; i++)
	{
		if (UMaterialInterface* Mat = ContainerMesh->GetMaterial(i))
		{
			GCComp->SetMaterial(i, Mat);
		}
	}

	// Collision
	GCComp->SetCollisionProfileName(GibCollisionProfile);
	GCComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	// Enable physics + gravity
	GCComp->SetSimulatePhysics(true);
	GCComp->SetEnableGravity(true);
	GCComp->RecreatePhysicsState();

	// Immediately shatter
	BreakContainer();
}

// ==================== Impact Detection (unused in current flow) ====================

void ARewardContainer::OnImpactProbeHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// Not used — container now explodes immediately on activation
}

// ==================== GC Destruction ====================

void ARewardContainer::BreakContainer()
{
	if (!SpawnedGCActor)
	{
		return;
	}

	UGeometryCollectionComponent* GCComp = SpawnedGCActor->GetGeometryCollectionComponent();
	if (!GCComp)
	{
		return;
	}

	// Shatter all clusters
	UUniformScalar* StrainField = NewObject<UUniformScalar>(SpawnedGCActor);
	StrainField->Magnitude = 999999.0f;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
		nullptr, StrainField);

	// Delay scatter impulse — bodies don't exist yet in the same frame as strain
	GetWorldTimerManager().SetTimer(ScatterImpulseHandle,
		this, &ARewardContainer::ApplyScatterImpulse, 0.05f, false);

	// VFX at container location
	const FVector BreakLocation = SpawnedGCActor->GetActorLocation();

	if (ImpactVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, ImpactVFX, BreakLocation,
			FRotator::ZeroRotator, FVector(ImpactVFXScale),
			true, true, ENCPoolMethod::None, true);
	}

	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, BreakLocation);
	}

	// Gib lifetime
	if (GibLifetime > 0.0f)
	{
		SpawnedGCActor->SetLifeSpan(GibLifetime);
	}

	// Schedule gib physics freeze
	if (GibFreezeTime > 0.0f)
	{
		TWeakObjectPtr<UGeometryCollectionComponent> WeakGC = GCComp;
		GetWorldTimerManager().SetTimer(GibFreezeHandle,
			FTimerDelegate::CreateLambda([WeakGC]()
			{
				if (UGeometryCollectionComponent* GC = WeakGC.Get())
				{
					GC->SetSimulatePhysics(false);
				}
			}),
			GibFreezeTime, false);
	}

	// Spawn reward
	SpawnReward();
}

// ==================== Scatter Impulse (Deferred) ====================

void ARewardContainer::ApplyScatterImpulse()
{
	if (!SpawnedGCActor)
	{
		return;
	}

	UGeometryCollectionComponent* GCComp = SpawnedGCActor->GetGeometryCollectionComponent();
	if (!GCComp)
	{
		return;
	}

	const FVector BreakOrigin = SpawnedGCActor->GetActorLocation();

	GCComp->AddRadialImpulse(BreakOrigin, BreakRadius, BreakImpulse,
		ERadialImpulseFalloff::RIF_Linear, /*bVelChange=*/ true);

	UE_LOG(LogTemp, Log, TEXT("RewardContainer %s: Applied radial impulse (Strength=%.0f, Radius=%.0f)"),
		*GetName(), BreakImpulse, BreakRadius);
}

// ==================== Reward Spawning ====================

void ARewardContainer::SpawnReward()
{
	if (!GetWorld())
	{
		return;
	}

	if (!RewardActorClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("RewardContainer %s: RewardActorClass not set, skipping reward spawn"), *GetName());
		return;
	}

	AActor* SpawnPointActor = RewardSpawnPoint.Get();
	if (!SpawnPointActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("RewardContainer %s: RewardSpawnPoint not set, skipping reward spawn"), *GetName());
		return;
	}

	const FVector SpawnLocation = SpawnPointActor->GetActorLocation();
	const FRotator SpawnRotation = SpawnPointActor->GetActorRotation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AActor* Reward = GetWorld()->SpawnActor<AActor>(RewardActorClass, SpawnLocation, SpawnRotation, SpawnParams);

	if (Reward)
	{
		UE_LOG(LogTemp, Log, TEXT("RewardContainer %s: Spawned reward %s at %s"),
			*GetName(), *Reward->GetName(), *SpawnLocation.ToString());
	}
}
