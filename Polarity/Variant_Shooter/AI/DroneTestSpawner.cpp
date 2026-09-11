// DroneTestSpawner.cpp

#include "DroneTestSpawner.h"

#include "Components/ArrowComponent.h"
#include "Engine/World.h"
#include "ShooterNPC.h"
#include "TimerManager.h"

ADroneTestSpawner::ADroneTestSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(SceneRoot);
}

void ADroneTestSpawner::BeginPlay()
{
	Super::BeginPlay();

	// Server only. The drone is a replicated NPC; a client spawning its own would get a second,
	// local drone that nobody else sees and that the server never hears about.
	if (HasAuthority())
	{
		SpawnNext();
	}
}

void ADroneTestSpawner::SpawnNext()
{
	if (!DroneClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DRONE_TEST] %s: DroneClass is not set, nothing to spawn"), *GetName());
		return;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// Location and facing only: a scaled spawner in the level must not scale the drone.
	const FTransform SpawnTransform(GetActorRotation(), GetActorLocation());
	AShooterNPC* Drone = GetWorld()->SpawnActor<AShooterNPC>(DroneClass, SpawnTransform, Params);
	if (!Drone)
	{
		UE_LOG(LogTemp, Error, TEXT("[DRONE_TEST] %s: failed to spawn %s"), *GetName(), *GetNameSafe(DroneClass));
		return;
	}

	if (!Drone->GetController())
	{
		Drone->SpawnDefaultController();
	}

	Drone->OnNPCDeath.AddDynamic(this, &ADroneTestSpawner::HandleDroneDeath);
	Drone->OnDestroyed.AddDynamic(this, &ADroneTestSpawner::HandleDroneDestroyed);
	CurrentDrone = Drone;
	++SpawnCount;

	UE_LOG(LogTemp, Log, TEXT("[DRONE_TEST] %s: spawned #%d %s"), *GetName(), SpawnCount, *Drone->GetName());
}

void ADroneTestSpawner::HandleDroneDeath(AShooterNPC* DeadNPC)
{
	if (CurrentDrone == DeadNPC)
	{
		ScheduleNext();
	}
}

void ADroneTestSpawner::HandleDroneDestroyed(AActor* DestroyedActor)
{
	// Fallback for a drone removed without dying (a debug kill, falling out of the world). After a
	// normal death CurrentDrone is already cleared, so the destroy that follows it does nothing.
	if (CurrentDrone == DestroyedActor)
	{
		ScheduleNext();
	}
}

void ADroneTestSpawner::ScheduleNext()
{
	CurrentDrone.Reset();

	// Never spawn from inside the dying drone's own callback: its death is still running, and the
	// next drone would be born into that same frame's explosion. The next frame at the earliest.
	if (RespawnDelay > 0.0f)
	{
		GetWorldTimerManager().SetTimer(RespawnTimer, this, &ADroneTestSpawner::SpawnNext, RespawnDelay, false);
	}
	else
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ADroneTestSpawner::SpawnNext);
	}
}
