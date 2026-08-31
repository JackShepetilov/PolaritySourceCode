// Banner.cpp

#include "Variant_Shooter/Map/Banner.h"

#include "Variant_Shooter/Map/PoiActor.h"
#include "AI/PolarityTeams.h"
#include "Coop/CoopPlayers.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
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

	// Left standing but inert: a banner that vanishes takes the landmark with it, and the team needs
	// to be able to see from across the map that this one is already done.
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	OnBroken.Broadcast(this);
}
