// KeepAliveSpawner.cpp

#include "KeepAliveSpawner.h"

#include "Components/ArrowComponent.h"
#include "Engine/World.h"
#include "ShooterNPC.h"
#include "TimerManager.h"

AKeepAliveSpawner::AKeepAliveSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(SceneRoot);
}

void AKeepAliveSpawner::BeginPlay()
{
	Super::BeginPlay();

	// Server only. The NPCs replicate; a client spawning its own would get a second, local copy that
	// nobody else sees and that the server never hears about.
	if (HasAuthority())
	{
		FillUp();
	}
}

void AKeepAliveSpawner::SetKeepAlive(int32 NewKeepAlive)
{
	KeepAlive = FMath::Max(0, NewKeepAlive);
	if (HasAuthority() && HasActorBegunPlay())
	{
		FillUp();
	}
}

void AKeepAliveSpawner::ResetBudget(int32 NewTotalToSpawn)
{
	TotalToSpawn = FMath::Max(-1, NewTotalToSpawn);
	BudgetSpent = 0;
	bClearedBroadcast = false;
	if (HasAuthority() && HasActorBegunPlay())
	{
		FillUp();
	}
}

void AKeepAliveSpawner::CheckCleared()
{
	if (!bClearedBroadcast && IsBudgetSpent() && GetAliveCount() == 0)
	{
		bClearedBroadcast = true;
		UE_LOG(LogTemp, Log, TEXT("[KEEP_ALIVE] %s: budget of %d spent, all dead"), *GetName(), TotalToSpawn);
		OnCleared.Broadcast(this);
	}
}

int32 AKeepAliveSpawner::GetAliveCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AShooterNPC>& NPC : Alive)
	{
		if (NPC.IsValid() && !NPC->IsDead())
		{
			++Count;
		}
	}
	return Count;
}

void AKeepAliveSpawner::DespawnAll()
{
	// Empty the list before destroying: HandleDestroyed then finds nothing to remove and schedules
	// no replacement, which is the point.
	TArray<TWeakObjectPtr<AShooterNPC>> Doomed = MoveTemp(Alive);
	Alive.Reset();

	int32 Destroyed = 0;
	for (const TWeakObjectPtr<AShooterNPC>& NPC : Doomed)
	{
		if (NPC.IsValid() && NPC->Destroy())
		{
			++Destroyed;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[KEEP_ALIVE] %s: despawned %d"), *GetName(), Destroyed);
}

void AKeepAliveSpawner::FillUp()
{
	const int32 Missing = KeepAlive - GetAliveCount();
	for (int32 i = 0; i < Missing; ++i)
	{
		SpawnNext();
	}
}

FTransform AKeepAliveSpawner::NextSpawnTransform()
{
	// Location and facing only: a scaled marker in the level must not scale the NPC.
	SpawnPoints.RemoveAll([](const TObjectPtr<AActor>& P) { return !IsValid(P); });
	if (SpawnPoints.Num() > 0)
	{
		const AActor* const Point = SpawnPoints[NextPointIndex % SpawnPoints.Num()];
		++NextPointIndex;
		return FTransform(Point->GetActorRotation(), Point->GetActorLocation());
	}

	// No points: at this actor, several at once spread sideways so they do not spawn inside each other.
	const FVector Side = GetActorRightVector() * (GetAliveCount() * 150.0f);
	return FTransform(GetActorRotation(), GetActorLocation() + Side);
}

void AKeepAliveSpawner::SpawnNext()
{
	Alive.RemoveAll([](const TWeakObjectPtr<AShooterNPC>& NPC) { return !NPC.IsValid() || NPC->IsDead(); });
	if (Alive.Num() >= KeepAlive)
	{
		return;
	}
	if (IsBudgetSpent())
	{
		CheckCleared();
		return;
	}

	if (!DroneClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[KEEP_ALIVE] %s: DroneClass is not set, nothing to spawn"), *GetName());
		return;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AShooterNPC* const NPC = GetWorld()->SpawnActor<AShooterNPC>(DroneClass, NextSpawnTransform(), Params);
	if (!NPC)
	{
		UE_LOG(LogTemp, Error, TEXT("[KEEP_ALIVE] %s: failed to spawn %s"), *GetName(), *GetNameSafe(DroneClass));
		return;
	}

	if (!NPC->GetController())
	{
		NPC->SpawnDefaultController();
	}

	NPC->OnNPCDeath.AddDynamic(this, &AKeepAliveSpawner::HandleDeath);
	NPC->OnDestroyed.AddDynamic(this, &AKeepAliveSpawner::HandleDestroyed);
	Alive.Add(NPC);
	++SpawnCount;
	++BudgetSpent;

	UE_LOG(LogTemp, Log, TEXT("[KEEP_ALIVE] %s: spawned #%d %s (%d/%d alive)"),
		*GetName(), SpawnCount, *NPC->GetName(), Alive.Num(), KeepAlive);
}

void AKeepAliveSpawner::HandleDeath(AShooterNPC* DeadNPC)
{
	if (Alive.Remove(DeadNPC) > 0)
	{
		ScheduleNext();
		CheckCleared();
	}
}

void AKeepAliveSpawner::HandleDestroyed(AActor* DestroyedActor)
{
	// Fallback for an NPC removed without dying (a carrier flying off to rearm, a debug kill, falling
	// out of the world). After a normal death it is already off the list, so this does nothing.
	const int32 Removed = Alive.RemoveAll([DestroyedActor](const TWeakObjectPtr<AShooterNPC>& NPC)
	{
		return NPC == DestroyedActor;
	});
	if (Removed > 0)
	{
		ScheduleNext();
		CheckCleared();
	}
}

void AKeepAliveSpawner::ScheduleNext()
{
	// Never spawn from inside the dying NPC's own callback: its death is still running. The next
	// frame at the earliest. Each death schedules its own replacement, so the timer is not shared.
	if (RespawnDelay > 0.0f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, this, &AKeepAliveSpawner::SpawnNext, RespawnDelay, false);
	}
	else
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &AKeepAliveSpawner::SpawnNext);
	}
}
