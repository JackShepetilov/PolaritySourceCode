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

	// Impact probe — invisible sphere, starts fully disabled
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

	ImpactProbe->SetSphereRadius(ImpactProbeRadius);
	ImpactProbe->OnComponentHit.AddDynamic(this, &ARewardContainer::OnImpactProbeHit);
}

void ARewardContainer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(GibFreezeHandle);
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

	UE_LOG(LogTemp, Log, TEXT("RewardContainer %s: Activated"), *GetName());

	// Hide the static mesh preview
	ContainerMesh->SetVisibility(false);

	// Spawn falling GC (whole, unbroken)
	SpawnFallingGC();

	// Activate the impact probe — detach from actor, enable physics, let it fall
	ImpactProbe->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	ImpactProbe->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ImpactProbe->SetCollisionObjectType(ECC_WorldDynamic);
	ImpactProbe->SetCollisionResponseToAllChannels(ECR_Ignore);
	ImpactProbe->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	ImpactProbe->BodyInstance.bNotifyRigidBodyCollision = true;
	ImpactProbe->SetSimulatePhysics(true);
	ImpactProbe->SetEnableGravity(true);
}

// ==================== GC Spawning ====================

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

	// Collision: block WorldStatic (ground), ignore pawns/camera
	GCComp->SetCollisionProfileName(GibCollisionProfile);
	GCComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	// Enable physics + gravity — NO strain field, GC falls as whole piece
	GCComp->SetSimulatePhysics(true);
	GCComp->SetEnableGravity(true);
	GCComp->RecreatePhysicsState();
}

// ==================== Impact Detection ====================

void ARewardContainer::OnImpactProbeHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bImpacted)
	{
		return;
	}
	bImpacted = true;

	ImpactLocation = Hit.ImpactPoint;

	UE_LOG(LogTemp, Log, TEXT("RewardContainer %s: Impact at %s"), *GetName(), *ImpactLocation.ToString());

	// Disable probe — its job is done
	ImpactProbe->SetSimulatePhysics(false);
	ImpactProbe->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ImpactProbe->DestroyComponent();

	BreakContainer();
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

	const FVector BreakOrigin = SpawnedGCActor->GetActorLocation();

	// Shatter all clusters
	UUniformScalar* StrainField = NewObject<UUniformScalar>(SpawnedGCActor);
	StrainField->Magnitude = 999999.0f;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
		nullptr, StrainField);

	// Radial scatter
	if (BreakImpulse > 0.0f)
	{
		URadialVector* RadialVelocity = NewObject<URadialVector>(SpawnedGCActor);
		RadialVelocity->Magnitude = BreakImpulse;
		RadialVelocity->Position = BreakOrigin;
		GCComp->ApplyPhysicsField(true,
			EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity,
			nullptr, RadialVelocity);
	}

	// Angular tumble
	if (BreakAngularImpulse > 0.0f)
	{
		URadialVector* AngularVelocity = NewObject<URadialVector>(SpawnedGCActor);
		AngularVelocity->Magnitude = BreakAngularImpulse;
		AngularVelocity->Position = BreakOrigin;
		GCComp->ApplyPhysicsField(true,
			EGeometryCollectionPhysicsTypeEnum::Chaos_AngularVelocity,
			nullptr, AngularVelocity);
	}

	// Impact VFX
	if (ImpactVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, ImpactVFX, ImpactLocation,
			FRotator::ZeroRotator, FVector(ImpactVFXScale),
			true, true, ENCPoolMethod::None, true);
	}

	// Impact SFX
	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, ImpactLocation);
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

// ==================== Reward Spawning ====================

void ARewardContainer::SpawnReward()
{
	if (!RewardActorClass || !GetWorld())
	{
		return;
	}

	const FVector SpawnLocation = ImpactLocation + FVector(0.0f, 0.0f, RewardSpawnZOffset);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AActor* Reward = GetWorld()->SpawnActor<AActor>(RewardActorClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams);

	if (Reward)
	{
		UE_LOG(LogTemp, Log, TEXT("RewardContainer %s: Spawned reward %s at %s"),
			*GetName(), *Reward->GetName(), *SpawnLocation.ToString());
	}
}
