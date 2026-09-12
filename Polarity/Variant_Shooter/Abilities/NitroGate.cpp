// NitroGate.cpp

#include "NitroGate.h"
#include "NitroGateSubsystem.h"
#include "Coop/CoopPlayers.h"
#include "PolarityCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

ANitroGate::ANitroGate()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicateMovement(true);

	FlightCollision = CreateDefaultSubobject<USphereComponent>(TEXT("FlightCollision"));
	FlightCollision->InitSphereRadius(18.0f);
	FlightCollision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	// Never bounce off the people it is thrown past. A gate that stuck to a teammate's shoulder
	// would be one thrown at a doorway and landing two metres short of it.
	FlightCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	FlightCollision->SetNotifyRigidBodyCollision(true);
	RootComponent = FlightCollision;

	PadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadMesh"));
	PadMesh->SetupAttachment(RootComponent);
	PadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	PadFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("PadFX"));
	PadFX->SetupAttachment(RootComponent);
	PadFX->SetAutoActivate(false);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->SetUpdatedComponent(FlightCollision);
	ProjectileMovement->bAutoActivate = false;
	ProjectileMovement->bShouldBounce = true;
	ProjectileMovement->Bounciness = 0.2f;
	ProjectileMovement->Friction = 0.6f;
	ProjectileMovement->ProjectileGravityScale = 1.0f;
	ProjectileMovement->bRotationFollowsVelocity = false;
}

void ANitroGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ANitroGate, bDeployed);
	DOREPLIFETIME(ANitroGate, DeployedLocation);
	DOREPLIFETIME(ANitroGate, DeployedYaw);
	DOREPLIFETIME(ANitroGate, BoostSpeed);
	DOREPLIFETIME(ANitroGate, PadRadius);
	DOREPLIFETIME(ANitroGate, bAffectsEnemies);
}

void ANitroGate::BeginPlay()
{
	Super::BeginPlay();

	FlightCollision->OnComponentHit.AddDynamic(this, &ANitroGate::OnFlightHit);

	// A gate that replicated in already deployed - a client joining late, or one that received the
	// whole actor in one bunch - gets no OnRep for a value that was already correct when it arrived.
	if (bDeployed)
	{
		ApplyDeployedState();
	}
}

void ANitroGate::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UWorld* World = GetWorld())
	{
		if (UNitroGateSubsystem* Gates = World->GetSubsystem<UNitroGateSubsystem>())
		{
			Gates->UnregisterGate(this);
		}
	}

	Super::EndPlay(Reason);
}

void ANitroGate::LaunchFrom(const FVector& Start, const FRotator& AimRotation, float ThrowSpeed,
	float InBoostSpeed, float InPadRadius, float InHealth, bool bInAffectsEnemies)
{
	BoostSpeed = InBoostSpeed;
	PadRadius = InPadRadius;
	Health = InHealth;
	bAffectsEnemies = bInAffectsEnemies;

	// Remembered now rather than at landing: it is the direction the THROW was aimed, and by the
	// time the gate lands the Tank is usually looking somewhere else.
	DeployedYaw = AimRotation.Yaw;

	SetActorLocation(Start);

	if (ProjectileMovement)
	{
		ProjectileMovement->Velocity = AimRotation.Vector() * ThrowSpeed;
		ProjectileMovement->Activate();
	}
}

void ANitroGate::OnFlightHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	if (bDeployed || !HasAuthority())
	{
		return;
	}

	// Horizontal enough to stand on, which is the same question the movement code asks of a floor.
	// Anything steeper is a wall: the gate bounces off it and tries again with whatever it lands on
	// next, so a throw into a corner still ends up on the ground rather than stuck to the wall.
	if (Hit.ImpactNormal.Z < 0.7f)
	{
		return;
	}

	Deploy(Hit.ImpactPoint, DeployedYaw);
}

void ANitroGate::Deploy(const FVector& Location, float Yaw)
{
	DeployedLocation = Location;
	DeployedYaw = Yaw;
	bDeployed = true;

	ApplyDeployedState();
}

void ANitroGate::OnRep_Deployed()
{
	ApplyDeployedState();
}

void ANitroGate::ApplyDeployedState()
{
	if (ProjectileMovement)
	{
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
	}

	// The transform comes from the replicated numbers, not from wherever movement replication left
	// this actor. Both ends have to agree on the pad's position to the centimetre: the launch test
	// is a distance from it, run separately on each machine, and a pad that sat 20cm apart on the
	// two ends would launch the player on different frames and cost a correction every time.
	SetActorLocation(DeployedLocation);
	SetActorRotation(FRotator(0.0f, DeployedYaw, 0.0f));
	SetReplicateMovement(false);

	if (FlightCollision)
	{
		FlightCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (PadFX)
	{
		if (DeployedFX)
		{
			PadFX->SetAsset(DeployedFX);
		}
		if (PadFX->GetAsset())
		{
			PadFX->Activate(true);
		}
	}

	if (DeploySound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, DeploySound, DeployedLocation);
	}

	if (UWorld* World = GetWorld())
	{
		if (UNitroGateSubsystem* Gates = World->GetSubsystem<UNitroGateSubsystem>())
		{
			Gates->RegisterGate(this);
		}
	}
}

bool ANitroGate::IsOnPad(const FVector& Feet) const
{
	if (!bDeployed)
	{
		return false;
	}

	const float HeightAbove = Feet.Z - DeployedLocation.Z;
	if (HeightAbove > PadHeightTolerance || HeightAbove < -PadHeightTolerance)
	{
		return false;
	}

	return FVector::DistSquared2D(Feet, DeployedLocation) <= PadRadius * PadRadius;
}

bool ANitroGate::WantsToBoost(const APolarityCharacter* Who) const
{
	if (!Who)
	{
		return false;
	}

	// The planned upgrade lands here: today an enemy gets the same launch a player does, and the
	// upgrade will give it a different outcome rather than no outcome. Keeping the question in one
	// function is what makes that a change to this body alone.
	return bAffectsEnemies || CoopPlayers::IsPlayer(Who);
}

void ANitroGate::NotifyLaunched(const FVector& Where)
{
	if (LaunchFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, LaunchFX, Where, FRotator(0.0f, DeployedYaw, 0.0f));
	}

	if (LaunchSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, LaunchSound, Where);
	}
}

float ANitroGate::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	if (!HasAuthority() || Applied <= 0.0f)
	{
		return Applied;
	}

	Health -= Applied;
	if (Health <= 0.0f)
	{
		Destroy();
	}

	return Applied;
}
