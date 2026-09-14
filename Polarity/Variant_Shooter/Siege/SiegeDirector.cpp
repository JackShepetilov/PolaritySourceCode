// SiegeDirector.cpp

#include "SiegeDirector.h"

#include "Arena/ArenaSpawnPoint.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Components/PrimitiveComponent.h"
#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "SiegeCoreBuildable.h"
#include "TimerManager.h"
#include "Variant_Shooter/AI/FlyingDrone.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "Variant_Shooter/AI/ShooterNPC.h"

ASiegeDirector::ASiegeDirector()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(5.0f);
}

void ASiegeDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASiegeDirector, CurrentWave);
	DOREPLIFETIME(ASiegeDirector, NextWaveServerTime);
	DOREPLIFETIME(ASiegeDirector, bSiegeActive);
	DOREPLIFETIME(ASiegeDirector, bBaseLost);
	DOREPLIFETIME(ASiegeDirector, AliveEnemies);
}

ASiegeDirector* ASiegeDirector::Get(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ASiegeDirector> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
}

// ==================== Lifecycle ====================

void ASiegeDirector::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	CollectSpawnPoints();
	CollectCores();
	RegisterStartTriggers();

	UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s ready: %d authored waves, %d endless kinds, %d spawn points, %d cores, interval %.0fs"),
		*GetName(), AuthoredWaves.Num(), EndlessPool.Num(), ResolvedSpawnPoints.Num(), ResolvedCores.Num(), WaveInterval);

	if (bStartOnBeginPlay)
	{
		StartSiege();
	}
}

void ASiegeDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(WaveTimer);
	Super::EndPlay(EndPlayReason);
}

void ASiegeDirector::CollectSpawnPoints()
{
	ResolvedSpawnPoints.Reset();
	for (const TSoftObjectPtr<AArenaSpawnPoint>& Soft : SpawnPoints)
	{
		if (AArenaSpawnPoint* const Point = Soft.Get())
		{
			ResolvedSpawnPoints.AddUnique(Point);
		}
	}
	if (bAutoCollectSpawnPoints)
	{
		for (TActorIterator<AArenaSpawnPoint> It(GetWorld()); It; ++It)
		{
			ResolvedSpawnPoints.AddUnique(*It);
		}
	}
}

void ASiegeDirector::CollectCores()
{
	ResolvedCores.Reset();
	for (const TSoftObjectPtr<ASiegeCoreBuildable>& Soft : Cores)
	{
		if (ASiegeCoreBuildable* const Core = Soft.Get())
		{
			ResolvedCores.AddUnique(Core);
		}
	}
	if (ResolvedCores.Num() == 0)
	{
		for (TActorIterator<ASiegeCoreBuildable> It(GetWorld()); It; ++It)
		{
			ResolvedCores.AddUnique(*It);
		}
	}
	for (const TWeakObjectPtr<ASiegeCoreBuildable>& Weak : ResolvedCores)
	{
		if (ASiegeCoreBuildable* const Core = Weak.Get())
		{
			Core->OnBuildableChanged.AddUniqueDynamic(this, &ASiegeDirector::OnCoreChanged);
		}
	}
}

void ASiegeDirector::RegisterStartTriggers()
{
	for (const TSoftObjectPtr<AActor>& Soft : StartTriggers)
	{
		AActor* const Trigger = Soft.Get();
		if (!Trigger)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SIEGE_DEBUG] %s: start trigger %s is not loaded"), *GetName(), *Soft.ToSoftObjectPath().ToString());
			continue;
		}
		// The first primitive is the volume, same as the arena's entry triggers.
		TArray<UPrimitiveComponent*> Primitives;
		Trigger->GetComponents<UPrimitiveComponent>(Primitives);
		if (Primitives.Num() > 0 && Primitives[0])
		{
			Primitives[0]->SetGenerateOverlapEvents(true);
			Primitives[0]->OnComponentBeginOverlap.AddUniqueDynamic(this, &ASiegeDirector::OnStartTriggerOverlap);
		}
	}
}

void ASiegeDirector::OnStartTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bSiegeActive && CoopPlayers::IsPlayer(OtherActor))
	{
		StartSiege();
	}
}

// ==================== Control ====================

void ASiegeDirector::StartSiege()
{
	if (!HasAuthority() || bSiegeActive || bBaseLost)
	{
		return;
	}
	bSiegeActive = true;
	CurrentWave = 0;
	OnSiegeStarted.Broadcast();
	UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: siege started, first wave in %.0fs"), *GetName(), FirstWaveDelay);
	ScheduleNextWave(FirstWaveDelay);
}

void ASiegeDirector::StopSiege()
{
	if (!HasAuthority())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(WaveTimer);
	bSiegeActive = false;
	NextWaveServerTime = 0.0f;
	UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: siege stopped at wave %d"), *GetName(), CurrentWave);
}

void ASiegeDirector::CallNextWaveNow()
{
	if (!HasAuthority() || !bSiegeActive)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(WaveTimer);
	StartWave();
}

float ASiegeDirector::GetSecondsToNextWave() const
{
	if (!bSiegeActive)
	{
		return 0.0f;
	}
	const UWorld* const World = GetWorld();
	const AGameStateBase* const GameState = World ? World->GetGameState() : nullptr;
	const float Now = GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0f);
	return FMath::Max(0.0f, NextWaveServerTime - Now);
}

void ASiegeDirector::ScheduleNextWave(float Delay)
{
	Delay = FMath::Max(0.0f, Delay);
	const UWorld* const World = GetWorld();
	const AGameStateBase* const GameState = World ? World->GetGameState() : nullptr;
	const float Now = GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0f);
	NextWaveServerTime = Now + Delay;
	if (Delay <= KINDA_SMALL_NUMBER)
	{
		// SetTimer with a zero rate clears the timer instead of firing it. Next tick, not now: the
		// caller may be an overlap callback, and a wave spawning inside it is asking for trouble.
		GetWorldTimerManager().ClearTimer(WaveTimer);
		WaveTimer = GetWorldTimerManager().SetTimerForNextTick(this, &ASiegeDirector::StartWave);
		return;
	}
	GetWorldTimerManager().SetTimer(WaveTimer, this, &ASiegeDirector::StartWave, Delay, false);
}

// ==================== Waves ====================

void ASiegeDirector::StartWave()
{
	if (!HasAuthority() || !bSiegeActive)
	{
		return;
	}

	const int32 WaveNumber = CurrentWave + 1;
	const int32 AuthoredIndex = WaveNumber - 1;
	const bool bAuthored = AuthoredWaves.IsValidIndex(AuthoredIndex);

	if (!bAuthored && EndlessPool.Num() == 0)
	{
		// Nothing left to send: the author wrote a finite siege.
		UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: authored waves spent and no endless pool, siege over after wave %d"), *GetName(), CurrentWave);
		StopSiege();
		return;
	}

	CurrentWave = WaveNumber;
	if (bAuthored)
	{
		SpawnAuthoredWave(AuthoredWaves[AuthoredIndex]);
	}
	else
	{
		SpawnEndlessWave(WaveNumber);
	}
	RefreshAliveCount();
	OnWaveStarted.Broadcast(WaveNumber);

	// The clock runs from the start of this wave, not from its death.
	const int32 NextAuthored = WaveNumber; // index of wave WaveNumber+1 in the list
	const float ExtraDelay = AuthoredWaves.IsValidIndex(NextAuthored) ? AuthoredWaves[NextAuthored].DelayBeforeWave : 0.0f;
	ScheduleNextWave(WaveInterval + ExtraDelay);
}

void ASiegeDirector::SpawnAuthoredWave(const FArenaWave& Wave)
{
	int32 Count = 0;
	for (const FArenaSpawnEntry& Entry : Wave.Entries)
	{
		if (!Entry.NPCClass)
		{
			continue;
		}
		for (int32 i = 0; i < Entry.Count; ++i)
		{
			SpawnOne(Entry.NPCClass);
			++Count;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: wave %d (authored) spawned %d, cost %.1f"), *GetName(), CurrentWave, Count, CostOfWave(Wave));
}

void ASiegeDirector::SpawnEndlessWave(int32 WaveNumber)
{
	float Budget = GetWaveBudget(WaveNumber);

	// The kinds this wave may buy, and the cheapest of them: when the remainder cannot afford
	// even that, the wave is spent.
	TArray<const FSiegeEnemyType*> Unlocked;
	float MinCost = TNumericLimits<float>::Max();
	float TotalWeight = 0.0f;
	for (const FSiegeEnemyType& Kind : EndlessPool)
	{
		if (Kind.NPCClass && WaveNumber >= Kind.FirstWave)
		{
			Unlocked.Add(&Kind);
			MinCost = FMath::Min(MinCost, Kind.Cost);
			TotalWeight += Kind.Weight;
		}
	}
	if (Unlocked.Num() == 0 || TotalWeight <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SIEGE_DEBUG] %s: wave %d has budget %.1f but nothing is unlocked yet"), *GetName(), WaveNumber, Budget);
		return;
	}

	int32 Count = 0;
	const float StartBudget = Budget;
	while (Budget >= MinCost && Count < MaxEnemiesPerWave)
	{
		// Weighted pick among the kinds the remainder can still afford.
		float Affordable = 0.0f;
		for (const FSiegeEnemyType* Kind : Unlocked)
		{
			if (Kind->Cost <= Budget)
			{
				Affordable += Kind->Weight;
			}
		}
		float Roll = FMath::FRand() * Affordable;
		const FSiegeEnemyType* Pick = nullptr;
		for (const FSiegeEnemyType* Kind : Unlocked)
		{
			if (Kind->Cost > Budget)
			{
				continue;
			}
			Roll -= Kind->Weight;
			if (Roll <= 0.0f)
			{
				Pick = Kind;
				break;
			}
		}
		if (!Pick)
		{
			break;
		}
		SpawnOne(Pick->NPCClass);
		Budget -= Pick->Cost;
		++Count;
	}
	UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: wave %d (endless) budget %.1f spawned %d, %.1f unspent"), *GetName(), WaveNumber, StartBudget, Count, Budget);
}

float ASiegeDirector::GetWaveBudget(int32 WaveNumber) const
{
	const int32 AuthoredCount = AuthoredWaves.Num();
	if (WaveNumber <= AuthoredCount)
	{
		return AuthoredWaves.IsValidIndex(WaveNumber - 1) ? CostOfWave(AuthoredWaves[WaveNumber - 1]) : 0.0f;
	}
	float First = FirstEndlessBudget;
	if (First <= 0.0f)
	{
		// Continue the author's curve: one growth step past the last written wave.
		First = (AuthoredCount > 0 ? CostOfWave(AuthoredWaves.Last()) : 1.0f) * EndlessBudgetGrowth;
	}
	const int32 StepsPastFirst = WaveNumber - AuthoredCount - 1;
	return First * FMath::Pow(EndlessBudgetGrowth, static_cast<float>(StepsPastFirst));
}

float ASiegeDirector::CostOf(TSubclassOf<AShooterNPC> NPCClass) const
{
	for (const FSiegeEnemyType& Kind : EndlessPool)
	{
		if (Kind.NPCClass && NPCClass && NPCClass->IsChildOf(Kind.NPCClass))
		{
			return Kind.Cost;
		}
	}
	return 1.0f;
}

float ASiegeDirector::CostOfWave(const FArenaWave& Wave) const
{
	float Total = 0.0f;
	for (const FArenaSpawnEntry& Entry : Wave.Entries)
	{
		Total += CostOf(Entry.NPCClass) * Entry.Count;
	}
	return Total;
}

// ==================== Spawning ====================

AArenaSpawnPoint* ASiegeDirector::PickSpawnPoint(TSubclassOf<AShooterNPC> NPCClass)
{
	// Round robin over the points that allow the class, so a wave spreads across the approaches.
	const int32 Num = ResolvedSpawnPoints.Num();
	for (int32 Tries = 0; Tries < Num; ++Tries)
	{
		AArenaSpawnPoint* const Point = ResolvedSpawnPoints[NextSpawnPointIndex % Num].Get();
		NextSpawnPointIndex = (NextSpawnPointIndex + 1) % Num;
		if (Point && Point->IsClassAllowed(NPCClass))
		{
			return Point;
		}
	}
	return nullptr;
}

void ASiegeDirector::SpawnOne(TSubclassOf<AShooterNPC> NPCClass)
{
	UWorld* const World = GetWorld();
	AArenaSpawnPoint* const Point = PickSpawnPoint(NPCClass);
	if (!World || !Point)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SIEGE_DEBUG] %s: no spawn point for %s"), *GetName(), *GetNameSafe(NPCClass));
		return;
	}

	const FTransform SpawnTransform = Point->ResolveSpawnTransformFor(NPCClass);
	APawn* const SpawnedPawn = UAIBlueprintHelperLibrary::SpawnAIFromClass(
		World, NPCClass, nullptr, SpawnTransform.GetLocation(), SpawnTransform.Rotator(), true);

	AShooterNPC* const NPC = Cast<AShooterNPC>(SpawnedPawn);
	if (!NPC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SIEGE_DEBUG] %s: spawn of %s at %s failed"), *GetName(), *GetNameSafe(NPCClass), *Point->GetName());
		return;
	}

	Alive.Add(NPC);
	NPC->OnNPCDeath.AddDynamic(this, &ASiegeDirector::OnEnemyDied);

	// A walker at the fog line has no line of sight to anybody, and an NPC that has not seen a
	// player never moves (AI_StateTree.md). Point it at the nearest one so it marches on the base;
	// its own perception takes over from there. The carriers drive themselves and are left alone.
	if (!NPCClass->IsChildOf(AFlyingDrone::StaticClass()))
	{
		if (AActor* const Nearest = CoopPlayers::GetNearest(World, NPC->GetActorLocation()))
		{
			if (AShooterAIController* const AIController = Cast<AShooterAIController>(NPC->GetController()))
			{
				AIController->SetCurrentTarget(Nearest);
				TWeakObjectPtr<AShooterAIController> WeakAIC = AIController;
				GetWorldTimerManager().SetTimerForNextTick([WeakAIC]()
				{
					if (AShooterAIController* const AIC = WeakAIC.Get())
					{
						AIC->ForcePerceptionUpdate();
					}
				});
			}
		}
	}
	UE_LOG(LogTemp, Verbose, TEXT("[SIEGE_DEBUG] %s: spawned %s at %s"), *GetName(), *NPC->GetName(), *Point->GetName());
}

void ASiegeDirector::RefreshAliveCount()
{
	Alive.RemoveAll([](const TWeakObjectPtr<AShooterNPC>& Ptr)
	{
		const AShooterNPC* const NPC = Ptr.Get();
		return !NPC || NPC->IsDead();
	});
	AliveEnemies = Alive.Num();
}

void ASiegeDirector::OnEnemyDied(AShooterNPC* DeadNPC)
{
	RefreshAliveCount();
}

// ==================== The base ====================

void ASiegeDirector::OnCoreChanged(ABuildableActor* Core)
{
	if (!HasAuthority() || bBaseLost)
	{
		return;
	}
	for (const TWeakObjectPtr<ASiegeCoreBuildable>& Weak : ResolvedCores)
	{
		const ASiegeCoreBuildable* const Other = Weak.Get();
		if (Other && !Other->IsDestroyed())
		{
			return;
		}
	}
	bBaseLost = true;
	UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: the last core fell in wave %d, base lost"), *GetName(), CurrentWave);
	StopSiege();
	OnBaseLost.Broadcast();
}
