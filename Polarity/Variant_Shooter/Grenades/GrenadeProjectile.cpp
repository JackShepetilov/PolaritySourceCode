// GrenadeProjectile.cpp

#include "Variant_Shooter/Grenades/GrenadeProjectile.h"
#include "Variant_Shooter/Grenades/GrenadeDefinition.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "AI/PolarityTeams.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/GameplayStaticsTypes.h"
#include "NiagaraFunctionLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "TimerManager.h"

AGrenadeProjectile::AGrenadeProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicateMovement(true);

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(8.0f);
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	// Rolls past legs rather than stopping on them: a grenade that lands on somebody's boot and drops
	// dead there is a throw that did not go where the arc said.
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->SetUpdatedComponent(Collision);
	Movement->bAutoActivate = false;
	Movement->bShouldBounce = true;
	Movement->Bounciness = 0.35f;
	Movement->Friction = 0.4f;
	Movement->BounceVelocityStopSimulatingThreshold = 40.0f;
	Movement->bRotationFollowsVelocity = false;
	Movement->bSweepCollision = true;
}

void AGrenadeProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGrenadeProjectile, Definition);
}

void AGrenadeProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (Movement)
	{
		Movement->OnProjectileBounce.AddDynamic(this, &AGrenadeProjectile::OnBounce);
	}

	// Never bounces off the one who threw it on the way out of his hand.
	if (APawn* const Thrower = GetInstigator())
	{
		Collision->IgnoreActorWhenMoving(Thrower, true);
	}
}

void AGrenadeProjectile::Launch(UGrenadeDefinition* InDefinition, const FVector& Velocity)
{
	UWorld* const World = GetWorld();
	if (!HasAuthority() || !InDefinition || !World)
	{
		return;
	}

	Definition = InDefinition;
	if (Movement)
	{
		Movement->ProjectileGravityScale = InDefinition->GravityScale;
		Movement->Velocity = Velocity;
		Movement->Activate();
	}

	DetonateAt = World->GetTimeSeconds() + InDefinition->FuseTime;
	World->GetTimerManager().SetTimer(FuseTimer, this, &AGrenadeProjectile::Detonate, InDefinition->FuseTime, false);

	PredictRest();
	World->GetTimerManager().SetTimer(WarnTimer, this, &AGrenadeProjectile::WarnAround, WarnInterval, true, 0.0f);
}

float AGrenadeProjectile::GetTimeToDetonate() const
{
	const UWorld* const World = GetWorld();
	return World ? FMath::Max(0.0f, DetonateAt - World->GetTimeSeconds()) : 0.0f;
}

void AGrenadeProjectile::PredictRest()
{
	// Where the flight comes down, by the same simulation the thrower's arc uses. The bounces after
	// it are left out: they are short, and each one asks again.
	DangerCenter = GetActorLocation();
	const UWorld* const World = GetWorld();
	if (!World || !Movement || Movement->Velocity.SizeSquared() < FMath::Square(150.0f))
	{
		return;
	}

	FPredictProjectilePathParams Params(Collision ? Collision->GetScaledSphereRadius() : 8.0f, GetActorLocation(),
		Movement->Velocity, FMath::Max(0.5f, GetTimeToDetonate()), ECC_Visibility, this);
	Params.OverrideGravityZ = World->GetGravityZ() * Movement->ProjectileGravityScale;
	Params.SimFrequency = 15.0f;
	if (APawn* const Thrower = GetInstigator())
	{
		Params.ActorsToIgnore.Add(Thrower);
	}

	FPredictProjectilePathResult Result;
	if (UGameplayStatics::PredictProjectilePath(this, Params, Result))
	{
		DangerCenter = Result.HitResult.ImpactPoint;
	}
	else if (Result.PathData.Num() > 0)
	{
		DangerCenter = Result.PathData.Last().Location;
	}
}

void AGrenadeProjectile::OnBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
	if (!HasAuthority() || bDetonated)
	{
		return;
	}

	if (const UWorld* const World = GetWorld())
	{
		LastBounceTime = World->GetTimeSeconds();
	}
	LastBounceLocation = ImpactResult.ImpactPoint;
	PredictRest();

	const float Strength = FMath::Clamp(ImpactVelocity.Size() / 800.0f, 0.15f, 1.0f);
	if (Strength > 0.15f)
	{
		Multicast_Bounce(ImpactResult.ImpactPoint, Strength);
	}
}

void AGrenadeProjectile::Multicast_Bounce_Implementation(FVector Location, float Strength)
{
	if (Definition && Definition->BounceSound && GetNetMode() != NM_DedicatedServer)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Definition->BounceSound, Location, Strength);
	}
}

bool AGrenadeProjectile::WasHeardBy(const FVector& ListenerLocation) const
{
	const UWorld* const World = GetWorld();
	return World && World->GetTimeSeconds() - LastBounceTime <= HeardWindow
		&& FVector::DistSquared(ListenerLocation, LastBounceLocation) <= FMath::Square(HearRadius);
}

void AGrenadeProjectile::ReportNoticedBy(const AActor* Noticer)
{
	const UWorld* const World = GetWorld();
	if (!World || !Noticer)
	{
		return;
	}
	const uint8 Side = PolarityTeams::GetTeam(Noticer);
	if (!NoticedAtBySide.Contains(Side))
	{
		NoticedAtBySide.Add(Side, World->GetTimeSeconds());
	}
}

bool AGrenadeProjectile::WasShoutedTo(const AActor* Listener) const
{
	const UWorld* const World = GetWorld();
	const float* const At = Listener ? NoticedAtBySide.Find(PolarityTeams::GetTeam(Listener)) : nullptr;
	return World && At && World->GetTimeSeconds() - *At >= ShoutDelay;
}

void AGrenadeProjectile::WarnAround()
{
	UWorld* const World = GetWorld();
	if (!HasAuthority() || bDetonated || !World || !Definition)
	{
		return;
	}

	// Lying still: the danger is where it lies.
	if (!Movement || !Movement->IsActive() || Movement->Velocity.SizeSquared() < FMath::Square(150.0f))
	{
		DangerCenter = GetActorLocation();
	}

	// A little wider than the danger: one standing just outside it still sees it coming at him.
	const float Radius = Definition->DangerRadius + 300.0f;
	FCollisionObjectQueryParams Objects(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GrenadeWarn), false, this);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, DangerCenter, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius), Params);

	TSet<AShooterNPC*> Told;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AShooterNPC* const NPC = Cast<AShooterNPC>(Overlap.GetActor());
		if (NPC && !Told.Contains(NPC))
		{
			Told.Add(NPC);
			NPC->NotifyGrenadeThreat(this, DangerCenter, Definition->DangerRadius, GetTimeToDetonate());
		}
	}
}

void AGrenadeProjectile::Detonate()
{
	UWorld* const World = GetWorld();
	if (!HasAuthority() || bDetonated || !World)
	{
		return;
	}
	bDetonated = true;

	World->GetTimerManager().ClearTimer(FuseTimer);
	World->GetTimerManager().ClearTimer(WarnTimer);
	if (Movement)
	{
		Movement->StopMovementImmediately();
		Movement->Deactivate();
	}

	// A little off the floor it lies on, so the lines drawn from it to whoever is near do not start
	// inside the ground.
	const FVector Origin = GetActorLocation() + FVector(0.0f, 0.0f, 20.0f);

	UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s (%s) goes off at %s"), *GetName(), *GetNameSafe(Definition), *Origin.ToCompactString());

	if (Definition)
	{
		Definition->Detonate(this, Origin);
	}
	Multicast_Detonate(Origin);

	// Gone from the picture now; the actor lives a moment longer so the multicast gets out with it.
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetLifeSpan(1.0f);
}

void AGrenadeProjectile::Multicast_Detonate_Implementation(FVector Origin)
{
	if (Mesh)
	{
		Mesh->SetVisibility(false);
	}

	if (!Definition || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (Definition->DetonationFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Definition->DetonationFX, Origin);
	}
	if (Definition->DetonationSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Definition->DetonationSound, Origin, FRotator::ZeroRotator,
			1.0f, 1.0f, 0.0f, Definition->DetonationAttenuation);
	}
}
