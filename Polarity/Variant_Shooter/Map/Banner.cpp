// Banner.cpp

#include "Variant_Shooter/Map/Banner.h"

#include "Variant_Shooter/Map/PoiActor.h"
#include "AI/PolarityTeams.h"
#include "Coop/CoopPlayers.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Field/FieldSystemObjects.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ABannerActor::ABannerActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));

	// A box out of the engine's basic shapes, so a banner is shootable the moment it is dropped in
	// the level and nobody has to make art before the mechanic can be played.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		Mesh->SetStaticMesh(CubeFinder.Object);
		Mesh->SetRelativeScale3D(FVector(1.2f, 1.2f, 3.0f));
	}

	SetCanBeDamaged(true);
}

void ABannerActor::SetOwningPoi(APoiActor* Poi)
{
	OwningPoi = Poi;
}

float ABannerActor::GetIntegrity() const
{
	return FMath::Clamp(Health / FMath::Max(MaxHealth, 1.0f), 0.0f, 1.0f);
}

float ABannerActor::TakeDamage(float Damage, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	if (bBroken || !HasAuthority() || Damage <= 0.0f)
	{
		return Applied;
	}

	// Who is actually responsible, resolved the way every other friendly-fire test in this project
	// resolves it: the pawn behind the rifle rather than the rifle.
	AActor* Source = PolarityTeams::ResolveDamageSource(DamageCauser, EventInstigator);
	if (bOnlyPlayersCanBreak && !CoopPlayers::IsPlayer(Source))
	{
		return Applied;
	}

	Health -= Damage;
	if (Health <= 0.0f)
	{
		Break(Source);
	}

	return Applied;
}

void ABannerActor::Break(AActor* Breaker)
{
	if (bBroken || !HasAuthority())
	{
		return;
	}

	bBroken = true;
	Health = 0.0f;

	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Banner %s broken by %s"),
		*GetName(), Breaker ? *Breaker->GetName() : TEXT("nobody"));

	const FVector BreakLocation = Mesh->GetComponentLocation();

	// The order is the whole reason this reads as an explosion rather than as a state change: the
	// banner comes apart, the blast goes off, and only then does the prize come out of it. Spill
	// the loot first and it is lying there before anything has happened to the thing holding it.
	ShatterIntoGibs();

	if (BreakVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, BreakVFX, BreakLocation, FRotator::ZeroRotator, FVector(BreakVFXScale),
			true, true, ENCPoolMethod::None, true);
	}

	if (BreakSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, BreakSound, BreakLocation);
	}

	// The point decides what breaking it costs. A banner that knew would need a branch per case,
	// and the cases belong to places, not to props.
	if (APoiActor* Poi = OwningPoi.Get())
	{
		Poi->NotifyBannerBroken(this, Breaker);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MAP_DEBUG] %s belongs to no point. Drag it into the point's Banner field."),
			*GetName());
	}

	OnBroken.Broadcast(this);
}

FVector ABannerActor::GetLootBurstOrigin() const
{
	return Mesh->GetComponentLocation() + FVector(0.0f, 0.0f, LootBurstHeight);
}

void ABannerActor::ShatterIntoGibs()
{
	// The standing mesh goes whatever happens. A banner that is still there after being broken is a
	// landmark that lies about the state of the map.
	Mesh->SetVisibility(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UWorld* World = GetWorld();
	if (!BannerGC || !World)
	{
		return;
	}

	const FTransform MeshTransform = Mesh->GetComponentTransform();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	SpawnedGibs = World->SpawnActor<AGeometryCollectionActor>(
		MeshTransform.GetLocation(), MeshTransform.GetRotation().Rotator(), SpawnParams);
	if (!SpawnedGibs)
	{
		return;
	}

	UGeometryCollectionComponent* Gibs = SpawnedGibs->GetGeometryCollectionComponent();
	if (!Gibs)
	{
		SpawnedGibs->Destroy();
		SpawnedGibs = nullptr;
		return;
	}

	SpawnedGibs->SetActorScale3D(MeshTransform.GetScale3D());
	Gibs->SetRestCollection(BannerGC);

	// Wearing the banner's own materials, so the pieces are recognisably the thing that just stood
	// there rather than grey rubble.
	const int32 NumMaterials = Mesh->GetNumMaterials();
	for (int32 Index = 0; Index < NumMaterials; ++Index)
	{
		if (UMaterialInterface* Material = Mesh->GetMaterial(Index))
		{
			Gibs->SetMaterial(Index, Material);
		}
	}

	Gibs->SetCollisionProfileName(GibCollisionProfile);
	Gibs->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Gibs->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Gibs->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	Gibs->SetSimulatePhysics(true);
	Gibs->SetEnableGravity(true);
	Gibs->RecreatePhysicsState();

	// Break every cluster at once.
	UUniformScalar* Strain = NewObject<UUniformScalar>(SpawnedGibs);
	Strain->Magnitude = 999999.0f;
	Gibs->ApplyPhysicsField(true, EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
		nullptr, Strain);

	GetWorldTimerManager().SetTimer(ScatterImpulseHandle, this,
		&ABannerActor::ApplyScatterImpulse, 0.05f, false);

	if (GibLifetime > 0.0f)
	{
		SpawnedGibs->SetLifeSpan(GibLifetime);
	}

	if (GibFreezeTime > 0.0f)
	{
		TWeakObjectPtr<UGeometryCollectionComponent> WeakGibs = Gibs;
		GetWorldTimerManager().SetTimer(GibFreezeHandle,
			FTimerDelegate::CreateLambda([WeakGibs]()
			{
				if (UGeometryCollectionComponent* Live = WeakGibs.Get())
				{
					Live->SetSimulatePhysics(false);
				}
			}),
			GibFreezeTime, false);
	}
}

void ABannerActor::ApplyScatterImpulse()
{
	if (!SpawnedGibs)
	{
		return;
	}

	if (UGeometryCollectionComponent* Gibs = SpawnedGibs->GetGeometryCollectionComponent())
	{
		Gibs->AddRadialImpulse(SpawnedGibs->GetActorLocation(), BreakRadius, BreakImpulse,
			ERadialImpulseFalloff::RIF_Linear, /*bVelChange=*/ true);
	}
}
