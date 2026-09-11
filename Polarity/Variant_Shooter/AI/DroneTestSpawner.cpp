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
		for (int32 i = 0; i < KeepAlive; ++i)
		{
			SpawnNext();
		}
	}
}

void ADroneTestSpawner::SpawnNext()
{
	AliveDrones.RemoveAll([](const TWeakObjectPtr<AShooterNPC>& D) { return !D.IsValid() || D->IsDead(); });
	if (AliveDrones.Num() >= KeepAlive)
	{
		return;
	}

	if (!DroneClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DRONE_TEST] %s: DroneClass is not set, nothing to spawn"), *GetName());
		return;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// Location and facing only: a scaled spawner in the level must not scale the drone. Several at
	// once are spread a little sideways so they do not spawn inside each other.
	const FVector Side = GetActorRightVector() * (AliveDrones.Num() * 150.0f);
	const FTransform SpawnTransform(GetActorRotation(), GetActorLocation() + Side);
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
	AliveDrones.Add(Drone);
	++SpawnCount;

	UE_LOG(LogTemp, Log, TEXT("[DRONE_TEST] %s: spawned #%d %s"), *GetName(), SpawnCount, *Drone->GetName());
}

void ADroneTestSpawner::HandleDroneDeath(AShooterNPC* DeadNPC)
{
	if (AliveDrones.Remove(DeadNPC) > 0)
	{
		ScheduleNext();
	}
}

void ADroneTestSpawner::HandleDroneDestroyed(AActor* DestroyedActor)
{
	// Fallback for a drone removed without dying (a debug kill, falling out of the world). After a
	// normal death it is already off the list, so the destroy that follows it does nothing.
	const int32 Removed = AliveDrones.RemoveAll([DestroyedActor](const TWeakObjectPtr<AShooterNPC>& D)
	{
		return D == DestroyedActor;
	});
	if (Removed > 0)
	{
		ScheduleNext();
	}
}

void ADroneTestSpawner::ScheduleNext()
{
	// Never spawn from inside the dying drone's own callback: its death is still running, and the
	// next drone would be born into that same frame's explosion. The next frame at the earliest.
	// Each death schedules its own replacement, so the timer is not shared between them.
	if (RespawnDelay > 0.0f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, this, &ADroneTestSpawner::SpawnNext, RespawnDelay, false);
	}
	else
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ADroneTestSpawner::SpawnNext);
	}
}
