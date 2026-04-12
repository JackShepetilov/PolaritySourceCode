// FlyingDroneSpawner.cpp

#include "FlyingDroneSpawner.h"
#include "FlyingDrone.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"

AFlyingDroneSpawner::AFlyingDroneSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
}

void AFlyingDroneSpawner::BeginPlay()
{
	Super::BeginPlay();
}

bool AFlyingDroneSpawner::RequestSpawn()
{
	if (ActiveDrone)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DRONE_SPAWNER] Spawn denied — drone already active"));
		return false;
	}

	if (!DroneClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[DRONE_SPAWNER] DroneClass not set!"));
		return false;
	}

	SpawnDrone();
	return true;
}

void AFlyingDroneSpawner::SpawnDrone()
{
	const FVector SpawnLocation = GetActorLocation() + FVector(0.f, 0.f, SpawnHeight);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AFlyingDrone* Drone = GetWorld()->SpawnActor<AFlyingDrone>(
		DroneClass, SpawnLocation, GetActorRotation(), SpawnParams);

	if (!Drone)
	{
		UE_LOG(LogTemp, Error, TEXT("[DRONE_SPAWNER] Failed to spawn drone"));
		return;
	}

	ActiveDrone = Drone;
	Drone->OnDestroyed.AddDynamic(this, &AFlyingDroneSpawner::OnActiveDroneDestroyed);

	// Launch impulse via CharacterMovementComponent
	if (LaunchSpeed > 0.f)
	{
		FVector WorldLaunchDir = GetActorTransform().TransformVectorNoScale(LaunchDirection.GetSafeNormal());

		if (LaunchSpreadAngle > 0.f)
		{
			WorldLaunchDir = FMath::VRandCone(WorldLaunchDir, FMath::DegreesToRadians(LaunchSpreadAngle));
		}

		if (UCharacterMovementComponent* CMC = Drone->GetCharacterMovement())
		{
			CMC->Velocity = WorldLaunchDir * LaunchSpeed;
		}
	}

	// VFX
	if (SpawnVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SpawnVFX, SpawnLocation);
	}

	// SFX
	if (SpawnSFX)
	{
		UGameplayStatics::PlaySoundAtLocation(this, SpawnSFX, SpawnLocation);
	}

	UE_LOG(LogTemp, Warning, TEXT("[DRONE_SPAWNER] Drone spawned: %s"), *Drone->GetName());

	OnDroneSpawned.Broadcast(Drone);
}

void AFlyingDroneSpawner::OnActiveDroneDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor == ActiveDrone)
	{
		ActiveDrone = nullptr;

		UE_LOG(LogTemp, Warning, TEXT("[DRONE_SPAWNER] Drone killed — button re-enabled"));

		OnDroneKilled.Broadcast();
	}
}
