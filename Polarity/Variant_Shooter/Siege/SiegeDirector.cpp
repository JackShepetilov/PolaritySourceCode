// SiegeDirector.cpp

#include "SiegeDirector.h"

#include "Arena/ArenaSpawnPoint.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "SiegeCoreBuildable.h"
#include "TimerManager.h"
#include "Variant_Shooter/AI/FlyingDrone.h"
#include "Variant_Shooter/AI/KamikazeCarrierDrone.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "Variant_Shooter/AI/ShooterNPC.h"

namespace
{
	/** How often the pulse runs (seconds). */
	constexpr float SiegePulseInterval = 0.25f;

	/** How long a spawn keeps the next one away from its spot (seconds). */
	constexpr float RecentSpawnMemory = 5.0f;

	/** Candidate bearings the air ring weighs per spawn. */
	constexpr int32 AirRingSamples = 36;

	float WrapDegrees(float Deg)
	{
		Deg = FMath::Fmod(Deg, 360.0f);
		return Deg < 0.0f ? Deg + 360.0f : Deg;
	}

	/** Shortest way round between two bearings, 0..180. */
	float AngularGap(float A, float B)
	{
		const float D = FMath::Abs(WrapDegrees(A) - WrapDegrees(B));
		return D > 180.0f ? 360.0f - D : D;
	}

	/** Length of an arc (X from, Y to, counterclockwise), 0..360. */
	float ArcSpan(const FVector2D& Arc)
	{
		const float Raw = Arc.Y - Arc.X;
		return Raw >= 360.0f ? 360.0f : WrapDegrees(Raw);
	}

	bool IsBearingInArcs(float Deg, const TArray<FVector2D>& Arcs)
	{
		if (Arcs.Num() == 0)
		{
			return true;
		}
		for (const FVector2D& Arc : Arcs)
		{
			if (WrapDegrees(Deg - Arc.X) <= ArcSpan(Arc))
			{
				return true;
			}
		}
		return false;
	}

	/** A uniformly random bearing inside the arcs, each arc weighted by its length. */
	float RandomBearingInArcs(const TArray<FVector2D>& Arcs)
	{
		float Total = 0.0f;
		for (const FVector2D& Arc : Arcs)
		{
			Total += ArcSpan(Arc);
		}
		if (Total <= KINDA_SMALL_NUMBER)
		{
			return FMath::FRand() * 360.0f;
		}
		float Roll = FMath::FRand() * Total;
		for (const FVector2D& Arc : Arcs)
		{
			const float Span = ArcSpan(Arc);
			if (Roll <= Span)
			{
				return WrapDegrees(Arc.X + Roll);
			}
			Roll -= Span;
		}
		return WrapDegrees(Arcs.Last().X);
	}

	FVector BearingToDirection(float Deg)
	{
		const float Rad = FMath::DegreesToRadians(Deg);
		return FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.0f);
	}

	bool IsCarrierClass(TSubclassOf<AShooterNPC> NPCClass)
	{
		return NPCClass && NPCClass->IsChildOf(AKamikazeCarrierDrone::StaticClass());
	}
}

ASiegeDirector::ASiegeDirector()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(5.0f);

	// The author's range (2026-09-24): four drones on a player at the start, eight by wave 13,
	// linear between. Any shape goes; the keys are the whole function.
	if (FRichCurve* const Curve = DronesPerTargetByWave.GetRichCurve())
	{
		Curve->SetKeyInterpMode(Curve->AddKey(1.0f, 4.0f), RCIM_Linear);
		Curve->SetKeyInterpMode(Curve->AddKey(13.0f, 8.0f), RCIM_Linear);
	}
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
	GetWorldTimerManager().ClearTimer(PulseTimer);
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
		RegisterCore(Soft.Get());
	}
	if (ResolvedCores.Num() == 0)
	{
		for (TActorIterator<ASiegeCoreBuildable> It(GetWorld()); It; ++It)
		{
			RegisterCore(*It);
		}
	}
}

void ASiegeDirector::RegisterCore(ABuildableActor* Core)
{
	if (!Core)
	{
		return;
	}
	ResolvedCores.AddUnique(Core);
	Core->OnBuildableChanged.AddUniqueDynamic(this, &ASiegeDirector::OnCoreChanged);
}

bool ASiegeDirector::HasStandingCore() const
{
	for (const TWeakObjectPtr<ABuildableActor>& Weak : ResolvedCores)
	{
		const ABuildableActor* const Core = Weak.Get();
		if (Core && !Core->IsDestroyed())
		{
			return true;
		}
	}
	return false;
}

void ASiegeDirector::NotifyBuildablePlaced(ABuildableActor* Building)
{
	if (!HasAuthority() || !bStartWhenDispenserBuilt || bBaseLost || !Building || !Building->IsDispenser())
	{
		return;
	}

	// The first one only. A second dispenser (another player's, in co-op) heals and bets like any
	// other but is not the base: there is one base and one clock.
	for (const TWeakObjectPtr<ABuildableActor>& Weak : ResolvedCores)
	{
		const ABuildableActor* const Core = Weak.Get();
		if (Core && Core != Building && Core->IsDispenser() && !Core->IsDestroyed())
		{
			return;
		}
	}

	Building->MakeSiegeCore();
	RegisterCore(Building);
	UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: %s is the base's core, starting the siege"), *GetName(), *Building->GetName());
	StartSiege();
}

void ASiegeDirector::RegisterStartTriggers()
{
	if (bStartWhenDispenserBuilt)
	{
		UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: waiting for the first dispenser, %d start triggers ignored"), *GetName(), StartTriggers.Num());
		return;
	}
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
	SpawnQueue.Reset();
	RecentSpawns.Reset();
	PacedSalvoCredit = 0.0f;
	bBaseQuiet = false;
	LastPulseTime = GetServerNow();
	GetWorldTimerManager().SetTimer(PulseTimer, this, &ASiegeDirector::Pulse, SiegePulseInterval, true);
	OnSiegeStarted.Broadcast();
	UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: siege started, first wave in %.0fs, spawn ring %s, carrier cap %d, paced drops %s"),
		*GetName(), FirstWaveDelay, bUseSpawnRing ? TEXT("on") : TEXT("off"), MaxCarriersAlive,
		bPaceCarrierSalvos ? TEXT("on") : TEXT("off"));
	ScheduleNextWave(FirstWaveDelay);
}

void ASiegeDirector::StopSiege()
{
	if (!HasAuthority())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(WaveTimer);
	GetWorldTimerManager().ClearTimer(PulseTimer);
	// Enemies not out yet never come; the carriers go back to their own cooldowns
	// (IsPacingCarriers is false from here).
	SpawnQueue.Reset();
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
	return FMath::Max(0.0f, NextWaveServerTime - GetServerNow());
}

float ASiegeDirector::GetServerNow() const
{
	const UWorld* const World = GetWorld();
	const AGameStateBase* const GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0f);
}

void ASiegeDirector::ScheduleNextWave(float Delay)
{
	Delay = FMath::Max(0.0f, Delay);
	NextWaveServerTime = GetServerNow() + Delay;
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

	// The drone cap is the wave's, so the carriers already up take it before anything new arrives.
	TArray<AKamikazeCarrierDrone*> Carriers;
	GatherLiveCarriers(Carriers);
	for (AKamikazeCarrierDrone* const Carrier : Carriers)
	{
		ApplyDronesPerTarget(Carrier);
	}

	const int32 QueuedBefore = SpawnQueue.Num();
	if (bAuthored)
	{
		SpawnAuthoredWave(AuthoredWaves[AuthoredIndex]);
	}
	else
	{
		SpawnEndlessWave(WaveNumber);
	}
	UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: wave %d queued %d (%d still waiting from before), %d alive, alive cost %.2f, %d carriers, drones per player %d"),
		*GetName(), WaveNumber, SpawnQueue.Num() - QueuedBefore, QueuedBefore, AliveEnemies, GetAliveCost(),
		Carriers.Num(), GetDronesPerTargetForWave(WaveNumber));

	// The first one comes out now, the rest on the trickle.
	TickSpawnQueue(GetServerNow());
	RefreshAliveCount();
	OnWaveStarted.Broadcast(WaveNumber);

	// The clock runs from the start of this wave, not from its death. A lull can bring it forward.
	ScheduleNextWave(WaveInterval + GetExtraDelayBefore(WaveNumber + 1));
}

float ASiegeDirector::GetExtraDelayBefore(int32 WaveNumber) const
{
	const int32 Index = WaveNumber - 1;
	return AuthoredWaves.IsValidIndex(Index) ? AuthoredWaves[Index].DelayBeforeWave : 0.0f;
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
			EnqueueSpawn(Entry.NPCClass);
			++Count;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: wave %d (authored) rolled %d, cost %.1f"), *GetName(), CurrentWave, Count, CostOfWave(Wave));
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
		EnqueueSpawn(Pick->NPCClass);
		Budget -= Pick->Cost;
		++Count;
	}
	UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: wave %d (endless) budget %.1f rolled %d, %.1f unspent"), *GetName(), WaveNumber, StartBudget, Count, Budget);
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

void ASiegeDirector::EnqueueSpawn(TSubclassOf<AShooterNPC> NPCClass)
{
	if (!NPCClass)
	{
		return;
	}
	if (SpawnQueue.Num() == 0)
	{
		// An idle queue starts now, not at a stale time from the last wave.
		NextTrickleTime = FMath::Max(NextTrickleTime, GetServerNow());
	}
	SpawnQueue.Add(NPCClass);
}

bool ASiegeDirector::SpawnOne(TSubclassOf<AShooterNPC> NPCClass)
{
	UWorld* const World = GetWorld();
	if (!World || !NPCClass)
	{
		return false;
	}

	if (TryAbsorbCarrierOverflow(NPCClass))
	{
		return false;
	}

	FTransform SpawnTransform;
	FString SpawnSource = TEXT("ring");
	if (!bUseSpawnRing || !FindRingSpawn(NPCClass, SpawnTransform))
	{
		AArenaSpawnPoint* const Point = PickSpawnPoint(NPCClass);
		if (!Point)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SIEGE_DEBUG] %s: no spawn %s for %s"), *GetName(),
				bUseSpawnRing ? TEXT("on the ring and no fallback point") : TEXT("point"), *GetNameSafe(NPCClass));
			return false;
		}
		if (bUseSpawnRing)
		{
			UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: ring found no spot for %s, fallback point %s"), *GetName(), *GetNameSafe(NPCClass), *Point->GetName());
		}
		SpawnTransform = Point->ResolveSpawnTransformFor(NPCClass);
		SpawnSource = Point->GetName();
	}

	APawn* const SpawnedPawn = UAIBlueprintHelperLibrary::SpawnAIFromClass(
		World, NPCClass, nullptr, SpawnTransform.GetLocation(), SpawnTransform.Rotator(), true);

	AShooterNPC* const NPC = Cast<AShooterNPC>(SpawnedPawn);
	if (!NPC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SIEGE_DEBUG] %s: spawn of %s at %s failed"), *GetName(), *GetNameSafe(NPCClass), *SpawnSource);
		return false;
	}

	Alive.Add(NPC);
	NPC->OnNPCDeath.AddDynamic(this, &ASiegeDirector::OnEnemyDied);

	FRecentSpawn& Recent = RecentSpawns.AddDefaulted_GetRef();
	Recent.Location = SpawnTransform.GetLocation();
	Recent.Time = GetServerNow();
	Recent.bAir = NPCClass->IsChildOf(AFlyingDrone::StaticClass());

	if (AKamikazeCarrierDrone* const Carrier = Cast<AKamikazeCarrierDrone>(NPC))
	{
		Carrier->SetSalvoPacer(this);
		ApplyDronesPerTarget(Carrier);
	}

	// A walker at the fog line has no line of sight to anybody, and an NPC that has not seen a
	// player never moves (AI_StateTree.md). The tower-defence rule names the base's core as its
	// first target, so its first step is taken now rather than on the controller's own 1s fallback
	// scan; perception takes over the moment a player shows. No core (a misconfigured map) keeps
	// the old nearest-player directive. The carriers drive themselves and are left alone.
	if (!NPCClass->IsChildOf(AFlyingDrone::StaticClass()))
	{
		AActor* MarchTarget = ABuildableActor::FindNearestCore(World, NPC->GetActorLocation());
		if (!MarchTarget)
		{
			MarchTarget = CoopPlayers::GetNearest(World, NPC->GetActorLocation());
		}
		if (MarchTarget)
		{
			if (AShooterAIController* const AIController = Cast<AShooterAIController>(NPC->GetController()))
			{
				AIController->SetCurrentTarget(MarchTarget);
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
	UE_LOG(LogTemp, Verbose, TEXT("[SIEGE_DEBUG] %s: spawned %s at %s (%.0f, %.0f, %.0f)"), *GetName(), *NPC->GetName(), *SpawnSource,
		SpawnTransform.GetLocation().X, SpawnTransform.GetLocation().Y, SpawnTransform.GetLocation().Z);
	return true;
}

// ==================== Spawn ring ====================

bool ASiegeDirector::FindRingSpawn(TSubclassOf<AShooterNPC> NPCClass, FTransform& OutTransform)
{
	return NPCClass->IsChildOf(AFlyingDrone::StaticClass())
		? FindAirRingSpawn(NPCClass, OutTransform)
		: FindGroundRingSpawn(NPCClass, OutTransform);
}

float ASiegeDirector::BearingOf(const FVector& Location) const
{
	const FVector Offset = Location - GetActorLocation();
	return WrapDegrees(FMath::RadiansToDegrees(FMath::Atan2(Offset.Y, Offset.X)));
}

bool ASiegeDirector::TraceGroundAt(const FVector& FlatLocation, FVector& OutGround) const
{
	const UWorld* const World = GetWorld();
	if (!World)
	{
		return false;
	}
	// The ring can run over hills and hollows well away from the base's height, so the trace
	// spans far both ways. World static only: a pawn or a drone under the spot is not the ground.
	const float BaseZ = GetActorLocation().Z;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SiegeRingGround), false, this);
	if (!World->LineTraceSingleByChannel(Hit, FVector(FlatLocation.X, FlatLocation.Y, BaseZ + 50000.0f),
		FVector(FlatLocation.X, FlatLocation.Y, BaseZ - 50000.0f), ECC_WorldStatic, Params))
	{
		return false;
	}
	OutGround = Hit.ImpactPoint;
	return true;
}

bool ASiegeDirector::IsSpawnSpotFree(const FVector& Location, float Radius, float HalfHeight) const
{
	const UWorld* const World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Nothing solid in the body's space: a rock, a wall, another pawn.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SiegeSpawnFree), false, this);
	if (World->OverlapBlockingTestByChannel(Location, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(Radius, HalfHeight), Params))
	{
		return false;
	}

	// Clear of the others, the live ones and whatever was put down a moment ago: a pawn spawned
	// this frame may not be in the overlap scene yet.
	const float MinDistSq = FMath::Square(FMath::Max(MinSpawnSeparation, Radius * 2.0f));
	for (const TWeakObjectPtr<AShooterNPC>& Weak : Alive)
	{
		const AShooterNPC* const Other = Weak.Get();
		if (Other && FVector::DistSquared(Other->GetActorLocation(), Location) < MinDistSq)
		{
			return false;
		}
	}
	for (const FRecentSpawn& Recent : RecentSpawns)
	{
		if (FVector::DistSquared(Recent.Location, Location) < MinDistSq)
		{
			return false;
		}
	}
	return true;
}

bool ASiegeDirector::FindGroundRingSpawn(TSubclassOf<AShooterNPC> NPCClass, FTransform& OutTransform)
{
	UWorld* const World = GetWorld();
	UNavigationSystemV1* const NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavSys)
	{
		return false;
	}

	const ACharacter* const CDO = NPCClass->GetDefaultObject<ACharacter>();
	const UCapsuleComponent* const Capsule = CDO ? CDO->GetCapsuleComponent() : nullptr;
	const float Radius = Capsule ? Capsule->GetUnscaledCapsuleRadius() : 34.0f;
	const float HalfHeight = Capsule ? Capsule->GetUnscaledCapsuleHalfHeight() : 96.0f;

	// Where a walker has to be able to get to: the core it will march on, or the base itself.
	const FVector Center = GetActorLocation();
	FNavLocation BaseNav;
	bool bCheckPath = false;
	if (bRequirePathToBase)
	{
		const AActor* const Core = ABuildableActor::FindNearestCore(World, Center);
		const FVector Goal = Core ? Core->GetActorLocation() : Center;
		bCheckPath = NavSys->ProjectPointToNavigation(Goal, BaseNav, FVector(1000.0f, 1000.0f, 2000.0f));
		if (!bCheckPath && !bWarnedNoBaseNav)
		{
			bWarnedNoBaseNav = true;
			UE_LOG(LogTemp, Warning, TEXT("[SIEGE_DEBUG] %s: no navmesh near the base goal (%.0f, %.0f, %.0f), ground spawns are not path-checked"),
				*GetName(), Goal.X, Goal.Y, Goal.Z);
		}
	}

	for (int32 Try = 0; Try < SpawnRingAttempts; ++Try)
	{
		const float Bearing = RandomBearingInArcs(GroundRingArcs);
		const float Distance = GroundRingRadius + FMath::FRandRange(-GroundRingDepth, GroundRingDepth);
		FVector Ground;
		if (!TraceGroundAt(Center + BearingToDirection(Bearing) * Distance, Ground))
		{
			continue;
		}
		FNavLocation Nav;
		if (!NavSys->ProjectPointToNavigation(Ground, Nav, FVector(200.0f, 200.0f, 500.0f)))
		{
			continue;
		}
		// A little above the navmesh: its surface floats up to a cell over the real ground.
		const FVector Location = Nav.Location + FVector(0.0f, 0.0f, HalfHeight + 20.0f);
		if (!IsSpawnSpotFree(Location, Radius, HalfHeight))
		{
			continue;
		}
		if (bCheckPath)
		{
			const UNavigationPath* const Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Nav.Location, BaseNav.Location);
			if (!Path || !Path->IsValid() || Path->IsPartial())
			{
				continue;
			}
		}
		const float FaceYaw = WrapDegrees(Bearing + 180.0f);
		OutTransform = FTransform(FRotator(0.0f, FaceYaw, 0.0f), Location);
		UE_LOG(LogTemp, Verbose, TEXT("[SIEGE_DEBUG] %s: ground ring spot for %s at bearing %.0f after %d tries"),
			*GetName(), *GetNameSafe(NPCClass), Bearing, Try + 1);
		return true;
	}
	return false;
}

bool ASiegeDirector::FindAirRingSpawn(TSubclassOf<AShooterNPC> NPCClass, FTransform& OutTransform)
{
	// The bearings already taken: every flyer up, where it is now, and the air spawns of the last
	// moments. The new one goes into the widest gap between them, so N carriers sit about 360/N
	// apart and none is ever born on top of another.
	TArray<float> Taken;
	for (const TWeakObjectPtr<AShooterNPC>& Weak : Alive)
	{
		const AShooterNPC* const Other = Weak.Get();
		if (Other && Other->IsA<AFlyingDrone>())
		{
			Taken.Add(BearingOf(Other->GetActorLocation()));
		}
	}
	for (const FRecentSpawn& Recent : RecentSpawns)
	{
		if (Recent.bAir)
		{
			Taken.Add(BearingOf(Recent.Location));
		}
	}

	struct FCandidate
	{
		float Bearing = 0.0f;
		float Gap = 0.0f;
	};
	TArray<FCandidate> Candidates;
	const float Step = 360.0f / AirRingSamples;
	for (int32 i = 0; i < AirRingSamples; ++i)
	{
		// Jittered, so two equal gaps are not always settled the same way.
		const float Bearing = WrapDegrees(i * Step + FMath::FRandRange(-0.5f, 0.5f) * Step);
		if (!IsBearingInArcs(Bearing, AirRingArcs))
		{
			continue;
		}
		float Gap = 180.0f;
		for (const float Other : Taken)
		{
			Gap = FMath::Min(Gap, AngularGap(Bearing, Other));
		}
		Candidates.Add(FCandidate{Bearing, Gap});
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Gap > B.Gap; });

	const ACharacter* const CDO = NPCClass->GetDefaultObject<ACharacter>();
	const UCapsuleComponent* const Capsule = CDO ? CDO->GetCapsuleComponent() : nullptr;
	const float Radius = Capsule ? Capsule->GetUnscaledCapsuleRadius() : 100.0f;
	const float HalfHeight = Capsule ? Capsule->GetUnscaledCapsuleHalfHeight() : 100.0f;
	const FVector Center = GetActorLocation();

	// The best few only: past them the gap is no longer the point.
	const int32 Tries = FMath::Min(Candidates.Num(), SpawnRingAttempts);
	for (int32 i = 0; i < Tries; ++i)
	{
		const FVector Flat = Center + BearingToDirection(Candidates[i].Bearing) * AirRingRadius;
		FVector Ground;
		const float GroundZ = TraceGroundAt(Flat, Ground) ? Ground.Z : Center.Z;
		const FVector Location(Flat.X, Flat.Y, GroundZ + AirRingHeight);
		if (!IsSpawnSpotFree(Location, Radius, HalfHeight))
		{
			continue;
		}
		OutTransform = FTransform(FRotator(0.0f, WrapDegrees(Candidates[i].Bearing + 180.0f), 0.0f), Location);
		UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: air ring spot for %s at bearing %.0f, %.0f deg from the nearest flyer (%d up)"),
			*GetName(), *GetNameSafe(NPCClass), Candidates[i].Bearing, Candidates[i].Gap, Taken.Num());
		return true;
	}
	return false;
}

// ==================== Carriers ====================

void ASiegeDirector::GatherLiveCarriers(TArray<AKamikazeCarrierDrone*>& OutCarriers) const
{
	OutCarriers.Reset();
	for (const TWeakObjectPtr<AShooterNPC>& Weak : Alive)
	{
		AKamikazeCarrierDrone* const Carrier = Cast<AKamikazeCarrierDrone>(Weak.Get());
		if (Carrier && !Carrier->IsDead())
		{
			OutCarriers.Add(Carrier);
		}
	}
}

bool ASiegeDirector::TryAbsorbCarrierOverflow(TSubclassOf<AShooterNPC> NPCClass)
{
	if (MaxCarriersAlive <= 0 || !IsCarrierClass(NPCClass))
	{
		return false;
	}
	TArray<AKamikazeCarrierDrone*> Carriers;
	GatherLiveCarriers(Carriers);
	if (Carriers.Num() < MaxCarriersAlive)
	{
		return false;
	}

	// Split evenly, in carriers' worth: a share of 1/N of this one's cost, counted against each
	// receiver's own cost, so a carrier kind twice as dear gains half as much from it.
	const float Cost = CostOf(NPCClass);
	for (AKamikazeCarrierDrone* const Carrier : Carriers)
	{
		const float ReceiverCost = FMath::Max(CostOf(Carrier->GetClass()), KINDA_SMALL_NUMBER);
		Carrier->AddSiegePower(Cost / Carriers.Num() / ReceiverCost);
	}
	UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: carrier cap %d reached in wave %d, %s (cost %.2f) split into the %d living ones"),
		*GetName(), MaxCarriersAlive, CurrentWave, *GetNameSafe(NPCClass), Cost, Carriers.Num());
	return true;
}

void ASiegeDirector::ApplyDronesPerTarget(AKamikazeCarrierDrone* Carrier) const
{
	const int32 DronesPerTarget = GetDronesPerTargetForWave(FMath::Max(CurrentWave, 1));
	if (Carrier && DronesPerTarget > 0)
	{
		Carrier->SetMaxDronesPerTarget(DronesPerTarget);
	}
}

int32 ASiegeDirector::GetDronesPerTargetForWave(int32 WaveNumber) const
{
	const FRichCurve* const Curve = DronesPerTargetByWave.GetRichCurveConst();
	if (!Curve || Curve->GetNumKeys() == 0)
	{
		return -1;
	}
	return FMath::Clamp(FMath::RoundToInt(Curve->Eval(static_cast<float>(WaveNumber))), 1, 20);
}

// ==================== Pulse ====================

void ASiegeDirector::Pulse()
{
	if (!HasAuthority() || !bSiegeActive)
	{
		return;
	}
	const float Now = GetServerNow();
	const float DeltaTime = FMath::Clamp(Now - LastPulseTime, 0.0f, 1.0f);
	LastPulseTime = Now;

	RecentSpawns.RemoveAll([Now](const FRecentSpawn& Recent) { return Now - Recent.Time > RecentSpawnMemory; });
	RefreshAliveCount();

	TickSpawnQueue(Now);
	TickCarrierPacing(DeltaTime);
	TickBreakLog(Now);
	TickLull(Now);
}

void ASiegeDirector::TickSpawnQueue(float Now)
{
	bool bSpawnedAny = false;
	while (SpawnQueue.Num() > 0 && (SpawnTrickleInterval <= KINDA_SMALL_NUMBER || Now >= NextTrickleTime))
	{
		const TSubclassOf<AShooterNPC> NPCClass = SpawnQueue[0];
		SpawnQueue.RemoveAt(0);
		// A carrier absorbed by the cap took no time: the next in line comes out at once.
		if (SpawnOne(NPCClass))
		{
			bSpawnedAny = true;
			NextTrickleTime = FMath::Max(NextTrickleTime + SpawnTrickleInterval, Now);
		}
	}
	if (bSpawnedAny)
	{
		RefreshAliveCount();
	}
}

float ASiegeDirector::GetAliveCost() const
{
	float Total = 0.0f;
	for (const TWeakObjectPtr<AShooterNPC>& Weak : Alive)
	{
		const AShooterNPC* const NPC = Weak.Get();
		if (NPC && !NPC->IsDead())
		{
			Total += CostOf(NPC->GetClass());
		}
	}
	return Total;
}

void ASiegeDirector::TickLull(float Now)
{
	if (!bEarlyWaveOnLull || CurrentWave <= 0 || SpawnQueue.Num() > 0)
	{
		return;
	}
	const float AliveCost = GetAliveCost();
	if (AliveCost > LullPressureCost + KINDA_SMALL_NUMBER)
	{
		return;
	}
	const float Delay = LullWaveDelay + GetExtraDelayBefore(CurrentWave + 1);
	// Already due sooner (the clock, or this very lull a pulse ago): nothing to bring forward.
	if (Now + Delay >= NextWaveServerTime - KINDA_SMALL_NUMBER)
	{
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: lull after wave %d (alive cost %.2f <= %.2f), wave %d in %.1fs instead of %.1fs"),
		*GetName(), CurrentWave, AliveCost, LullPressureCost, CurrentWave + 1, Delay, NextWaveServerTime - Now);
	ScheduleNextWave(Delay);
}

void ASiegeDirector::TickCarrierPacing(float DeltaTime)
{
	if (!IsPacingCarriers())
	{
		PacedSalvoCredit = 0.0f;
		return;
	}
	TArray<AKamikazeCarrierDrone*> Carriers;
	GatherLiveCarriers(Carriers);
	if (Carriers.Num() == 0)
	{
		PacedSalvoCredit = 0.0f;
		return;
	}

	// One rate for the whole swarm: each carrier adds power / its own cooldown salvos a second,
	// capped at one per MinCarrierSalvoInterval, past which its power only buys shield.
	const float MaxPerCarrier = 1.0f / FMath::Max(MinCarrierSalvoInterval, 0.5f);
	float Rate = 0.0f;
	for (const AKamikazeCarrierDrone* const Carrier : Carriers)
	{
		Rate += FMath::Min(Carrier->GetSiegePower() / FMath::Max(Carrier->GetBaseSalvoCooldown(), 0.5f), MaxPerCarrier);
	}
	PacedSalvoCredit += Rate * DeltaTime;

	while (PacedSalvoCredit >= 1.0f)
	{
		// Who can drop now, weighted by power: the strong carriers drop more often, so the one to
		// kill first is the one the swarm comes from.
		TArray<AKamikazeCarrierDrone*> Ready;
		float TotalPower = 0.0f;
		for (AKamikazeCarrierDrone* const Carrier : Carriers)
		{
			if (Carrier->IsReadyForPacedSalvo(MinCarrierSalvoInterval))
			{
				Ready.Add(Carrier);
				TotalPower += Carrier->GetSiegePower();
			}
		}
		if (Ready.Num() == 0)
		{
			// Owed, not banked: one salvo waits for the first carrier to get ready, the rest of the
			// time nobody could drop is lost rather than dumped all at once later.
			PacedSalvoCredit = 1.0f;
			return;
		}
		float Roll = FMath::FRand() * TotalPower;
		AKamikazeCarrierDrone* Pick = Ready.Last();
		for (AKamikazeCarrierDrone* const Carrier : Ready)
		{
			Roll -= Carrier->GetSiegePower();
			if (Roll <= 0.0f)
			{
				Pick = Carrier;
				break;
			}
		}
		if (!Pick->DeployPacedSalvo(MinCarrierSalvoInterval))
		{
			// Ready a moment ago and not now (it just started one, or lost sight): try the others.
			Carriers.Remove(Pick);
			if (Carriers.Num() == 0)
			{
				PacedSalvoCredit = 1.0f;
				return;
			}
			continue;
		}
		PacedSalvoCredit -= 1.0f;
		UE_LOG(LogTemp, Verbose, TEXT("[SIEGE_DEBUG] %s: paced salvo from %s (power %.2f), swarm rate %.3f/s"),
			*GetName(), *Pick->GetName(), Pick->GetSiegePower(), Rate);
	}
}

void ASiegeDirector::TickBreakLog(float Now)
{
	const FVector Center = GetActorLocation();
	const float RadiusSq = FMath::Square(BaseRingRadius);
	bool bAnyAtBase = false;
	for (const TWeakObjectPtr<AShooterNPC>& Weak : Alive)
	{
		const AShooterNPC* const NPC = Weak.Get();
		if (NPC && !NPC->IsDead() && FVector::DistSquared2D(NPC->GetActorLocation(), Center) <= RadiusSq)
		{
			bAnyAtBase = true;
			break;
		}
	}
	if (!bAnyAtBase && !bBaseQuiet)
	{
		bBaseQuiet = true;
		BaseQuietSince = Now;
	}
	else if (bAnyAtBase && bBaseQuiet)
	{
		bBaseQuiet = false;
		UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: break at base %.1fs, ended in wave %d"), *GetName(), Now - BaseQuietSince, CurrentWave);
	}
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
	if (HasStandingCore())
	{
		return;
	}
	bBaseLost = true;
	UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s: the last core fell in wave %d, base lost"), *GetName(), CurrentWave);
	StopSiege();
	OnBaseLost.Broadcast();
}
