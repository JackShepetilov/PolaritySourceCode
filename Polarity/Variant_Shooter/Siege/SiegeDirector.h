// SiegeDirector.h
// The clock of the base defense.
//
// Waves come on a timer, never on a body count: the next one is due WaveInterval seconds after
// the last whether or not the last is dead. A player who leaves the base is not waited for, the
// enemy piles up at home and that pile is what calls them back. Nothing here resets on a death,
// nothing walls the arena off, nothing saves a checkpoint: that was AArenaManager's job for the
// old roguelike arenas, and the reasons it did not fit a siege are in
// Docs/Siege_Director_Plan_2026-09-14.md.
//
// Two phases. The AUTHORED waves are a hand-written list, one FArenaWave each, the same struct
// the arenas use. When they run out the ENDLESS phase begins: every wave gets a budget that grows
// by EndlessBudgetGrowth per wave, and spends it on kinds from EndlessPool by cost, weight and the
// wave each kind unlocks at. Growth above 1 is deliberate: a wave that only grows linearly is
// outrun by the metal it drops, and an endless mode has to end.
//
// The clock is a ceiling, not the only trigger: when the base goes quiet the next wave is sent
// early (bEarlyWaveOnLull), so the break a player gets is the walk in from the fog, not a timer.
// A wave comes out one enemy at a time (SpawnTrickleInterval), on a ring around the director
// rather than on placed points, and the carriers are capped: one rolled past MaxCarriersAlive
// becomes power for the living ones, and one clock here paces every carrier's drops.
//
// Server only: it spawns and it counts. The few numbers a HUD wants are replicated.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Arena/ArenaWaveData.h"
#include "Curves/CurveFloat.h"
#include "SiegeDirector.generated.h"

class AArenaSpawnPoint;
class ABuildableActor;
class AKamikazeCarrierDrone;
class AShooterNPC;
class ASiegeCoreBuildable;
class UPrimitiveComponent;

/** One kind of enemy the endless phase may buy. */
USTRUCT(BlueprintType)
struct FSiegeEnemyType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege")
	TSubclassOf<AShooterNPC> NPCClass;

	/** What one of these costs out of the wave budget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege", meta = (ClampMin = "0.01"))
	float Cost = 1.0f;

	/** First wave this kind may appear in, counting every wave of the siege from 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege", meta = (ClampMin = "1"))
	int32 FirstWave = 1;

	/** Relative pick weight among the kinds already unlocked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege", meta = (ClampMin = "0.01"))
	float Weight = 1.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSiegeStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSiegeWaveStarted, int32, WaveNumber);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSiegeBaseLost);

UCLASS(Blueprintable)
class POLARITY_API ASiegeDirector : public AActor
{
	GENERATED_BODY()

public:

	ASiegeDirector();

	// ==================== Authored waves ====================

	/** Hand-written waves, in order. Wave N of the siege is entry N-1 here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Waves")
	TArray<FArenaWave> AuthoredWaves;

	// ==================== Endless waves ====================

	/** What the endless phase may spend its budget on. Empty = the siege stops after the authored
	 *  waves. A kind absent from this list costs 1 when the last authored wave is priced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Endless")
	TArray<FSiegeEnemyType> EndlessPool;

	/** Budget of each endless wave over the one before. 1.0 = the same every wave (never ends);
	 *  1.5 doubles every second wave (the author's number, 2026-09-14). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Endless", meta = (ClampMin = "1.0"))
	float EndlessBudgetGrowth = 1.5f;

	/** Budget of the first endless wave. 0 = the last authored wave's cost times the growth, so the
	 *  curve continues from where the author left it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Endless", meta = (ClampMin = "0.0"))
	float FirstEndlessBudget = 0.0f;

	/** Hard cap on what one endless wave may spawn, whatever the budget says. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Endless", meta = (ClampMin = "1"))
	int32 MaxEnemiesPerWave = 40;

	// ==================== Clock ====================

	/** From the siege starting to the first wave (seconds). Time to look around and build. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Clock", meta = (ClampMin = "0.0", Units = "s"))
	float FirstWaveDelay = 0.0f;

	/** From one wave starting to the next (seconds). The last wave being alive does not delay it:
	 *  they stack, and the stack is the pressure. An authored wave's DelayBeforeWave is added. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Clock", meta = (ClampMin = "1.0", Units = "s"))
	float WaveInterval = 20.0f;

	// ==================== Pacing ====================

	/** Send the next wave early when the base goes quiet. The clock stays the ceiling: a player
	 *  away on a raid does not thin the enemy out, so the pile at home keeps growing on the clock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Pacing")
	bool bEarlyWaveOnLull = true;

	/** Quiet = the budget cost of every enemy this director spawned and is still alive is at or
	 *  under this (a carrier costs 1, a shooter 0.25 on L_HomeBase). All of them count, not only the
	 *  ones near the base: a wave still walking in from the fog is not a lull. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Pacing", meta = (ClampMin = "0.0", EditCondition = "bEarlyWaveOnLull"))
	float LullPressureCost = 0.25f;

	/** From the lull to the early wave (seconds). The walk in from the spawn ring comes on top, and
	 *  that walk is most of the time the player has to build. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Pacing", meta = (ClampMin = "0.0", Units = "s", EditCondition = "bEarlyWaveOnLull"))
	float LullWaveDelay = 2.0f;

	/** A wave comes out one enemy at a time, this far apart (seconds). 0 = the whole wave at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Pacing", meta = (ClampMin = "0.0", Units = "s"))
	float SpawnTrickleInterval = 0.75f;

	/** Measuring only: an enemy within this of the director counts as "at the base", and every
	 *  stretch with none is logged as a break ([SIEGE_DEBUG] break at base). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Pacing", meta = (ClampMin = "0.0", Units = "cm"))
	float BaseRingRadius = 6000.0f;

	// ==================== Spawn ====================

	/** Spawn on a ring around this director instead of on placed points: a walker on a random
	 *  reachable spot of the ground ring, a flyer in the widest gap between the flyers already up.
	 *  The placed points stay as the fallback for when the ring finds nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn")
	bool bUseSpawnRing = true;

	/** Distance of the ground ring from the director (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bUseSpawnRing"))
	float GroundRingRadius = 9000.0f;

	/** A walker lands up to this much nearer or farther than GroundRingRadius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bUseSpawnRing"))
	float GroundRingDepth = 1000.0f;

	/** Where on the ground ring walkers may appear: X from, Y to, in degrees counterclockwise seen
	 *  from above, 0 = +X (east), 90 = +Y. (180, 360) is the western half. Empty = the whole ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (EditCondition = "bUseSpawnRing"))
	TArray<FVector2D> GroundRingArcs;

	/** Distance of the air ring from the director (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bUseSpawnRing"))
	float AirRingRadius = 16000.0f;

	/** A flyer appears this high above the ground under it (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bUseSpawnRing"))
	float AirRingHeight = 3500.0f;

	/** Same as GroundRingArcs, for flyers. Empty = the whole ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (EditCondition = "bUseSpawnRing"))
	TArray<FVector2D> AirRingArcs;

	/** No spawn nearer than this to a live enemy or to anything spawned in the last few seconds (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bUseSpawnRing"))
	float MinSpawnSeparation = 500.0f;

	/** Random spots a walker's spawn tries before it falls back to the placed points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (ClampMin = "1", ClampMax = "32", EditCondition = "bUseSpawnRing"))
	int32 SpawnRingAttempts = 8;

	/** A walker's spot must have a full navmesh path to the base's core (or to the director when
	 *  there is no core yet), so nobody is born in a pit or on a ledge. One path query per try. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (EditCondition = "bUseSpawnRing"))
	bool bRequirePathToBase = true;

	/** Where the enemy enters when the ring is off or finds nothing. The same markers the arenas
	 *  use, air spawn included. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn")
	TArray<TSoftObjectPtr<AArenaSpawnPoint>> SpawnPoints;

	/** Also take every AArenaSpawnPoint found in the world at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn")
	bool bAutoCollectSpawnPoints = true;

	// ==================== Carriers ====================

	/** At most this many carriers alive at once. A carrier rolled past it is not spawned: its cost
	 *  is split evenly between the living carriers as power (more shield, more drops), so the field
	 *  holds as much carrier as the budget bought, in fewer bodies. 0 = no cap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Carriers", meta = (ClampMin = "0"))
	int32 MaxCarriersAlive = 4;

	/** One clock for every carrier's drops instead of each carrier's own cooldown. Its rate is the
	 *  sum over living carriers of power / SalvoCooldown, and each tick of it goes to one carrier
	 *  that is ready (in position, in sight, target not full), picked by power. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Carriers")
	bool bPaceCarrierSalvos = true;

	/** Shortest gap between two salvos of one carrier (seconds). Past it a strong carrier's extra
	 *  power stops adding drops and only its shield keeps growing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Carriers", meta = (ClampMin = "0.5", Units = "s", EditCondition = "bPaceCarrierSalvos"))
	float MinCarrierSalvoInterval = 4.0f;

	/** X: wave number. Y: kamikaze drones allowed on one player at once (rounded, 1..20). Applied
	 *  to every carrier at each wave start and to each new one. No keys = the carriers' own
	 *  MaxDronesPerTarget. A dive on an undefended core is never capped by this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Carriers")
	FRuntimeFloatCurve DronesPerTargetByWave;

	// ==================== Start ====================

	/** The first dispenser a player builds becomes the base's core and starts the siege
	 *  (Docs/Dispenser_Core_Refinery_Plan_2026-09-22.md). While on, StartTriggers are not listened
	 *  to: putting the base down is the "ready". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Start")
	bool bStartWhenDispenserBuilt = true;

	/** Actors whose overlap by a player starts the siege (a console in the house). Ignored while
	 *  bStartWhenDispenserBuilt is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Start")
	TArray<TSoftObjectPtr<AActor>> StartTriggers;

	/** Start the clock at BeginPlay instead of waiting for a trigger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Start")
	bool bStartOnBeginPlay = false;

	// ==================== Base ====================

	/** Level-placed cores. Empty = every ASiegeCoreBuildable in the world at BeginPlay. A dispenser
	 *  that becomes the core joins them at runtime. When the last core falls the base is lost and
	 *  the clock stops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Base")
	TArray<TSoftObjectPtr<ASiegeCoreBuildable>> Cores;

	// ==================== State ====================

	/** Wave number of the wave most recently started, from 1. 0 before the first. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Siege|State")
	int32 CurrentWave = 0;

	/** Server world time the next wave is due at. See GetSecondsToNextWave. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Siege|State")
	float NextWaveServerTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Siege|State")
	bool bSiegeActive = false;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Siege|State")
	bool bBaseLost = false;

	/** Enemies this director spawned that are still alive. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Siege|State")
	int32 AliveEnemies = 0;

	UFUNCTION(BlueprintPure, Category = "Siege")
	float GetSecondsToNextWave() const;

	/** True once the authored list is spent. */
	UFUNCTION(BlueprintPure, Category = "Siege")
	bool IsEndless() const { return CurrentWave > AuthoredWaves.Num(); }

	/** Budget an endless wave of this number gets. Authored numbers return their list cost. */
	UFUNCTION(BlueprintPure, Category = "Siege")
	float GetWaveBudget(int32 WaveNumber) const;

	/** DronesPerTargetByWave at this wave, rounded and clamped. -1 = the curve has no keys. */
	UFUNCTION(BlueprintPure, Category = "Siege")
	int32 GetDronesPerTargetForWave(int32 WaveNumber) const;

	/** The carriers this director spawned should drop on its clock, not their own. */
	bool IsPacingCarriers() const { return bPaceCarrierSalvos && bSiegeActive; }

	UPROPERTY(BlueprintAssignable, Category = "Siege|Events")
	FOnSiegeStarted OnSiegeStarted;

	UPROPERTY(BlueprintAssignable, Category = "Siege|Events")
	FOnSiegeWaveStarted OnWaveStarted;

	UPROPERTY(BlueprintAssignable, Category = "Siege|Events")
	FOnSiegeBaseLost OnBaseLost;

	// ==================== Control (server) ====================

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Siege")
	void StartSiege();

	/** Stop the clock. Whatever is alive stays alive. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Siege")
	void StopSiege();

	/** Skip the wait: the next wave starts now and the clock restarts from it. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Siege")
	void CallNextWaveNow();

	/** A player's building has just been put down. The first dispenser on this map becomes the
	 *  core and starts the siege, when bStartWhenDispenserBuilt. Server only. */
	void NotifyBuildablePlaced(ABuildableActor* Building);

	/** The director of this world, or null. One per level. */
	static ASiegeDirector* Get(const UWorld* World);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	void CollectSpawnPoints();
	void CollectCores();
	void RegisterStartTriggers();

	/** Watch Core for its fall. */
	void RegisterCore(ABuildableActor* Core);

	/** A core in the list is still standing. */
	bool HasStandingCore() const;

	void ScheduleNextWave(float Delay);
	void StartWave();
	void SpawnAuthoredWave(const FArenaWave& Wave);
	void SpawnEndlessWave(int32 WaveNumber);

	/** Put one enemy in the trickle queue. */
	void EnqueueSpawn(TSubclassOf<AShooterNPC> NPCClass);

	/** Spawn now. False when it was not spawned: no spot found, or a carrier past the cap (whose
	 *  cost then went to the living carriers). */
	bool SpawnOne(TSubclassOf<AShooterNPC> NPCClass);
	AArenaSpawnPoint* PickSpawnPoint(TSubclassOf<AShooterNPC> NPCClass);

	// ---- Spawn ring ----

	bool FindRingSpawn(TSubclassOf<AShooterNPC> NPCClass, FTransform& OutTransform);
	bool FindGroundRingSpawn(TSubclassOf<AShooterNPC> NPCClass, FTransform& OutTransform);
	bool FindAirRingSpawn(TSubclassOf<AShooterNPC> NPCClass, FTransform& OutTransform);

	/** Nothing solid in a body of this size here, and no live enemy or fresh spawn too near. */
	bool IsSpawnSpotFree(const FVector& Location, float Radius, float HalfHeight) const;

	/** The ground under this XY, traced from far above and below the director. */
	bool TraceGroundAt(const FVector& FlatLocation, FVector& OutGround) const;

	/** Bearing of a location around this director, degrees 0..360, 0 = +X. */
	float BearingOf(const FVector& Location) const;

	// ---- Carriers ----

	/** Live carriers this director spawned. */
	void GatherLiveCarriers(TArray<AKamikazeCarrierDrone*>& OutCarriers) const;

	/** A carrier past MaxCarriersAlive: hand its cost to the living ones. True = absorbed, do not spawn. */
	bool TryAbsorbCarrierOverflow(TSubclassOf<AShooterNPC> NPCClass);

	void ApplyDronesPerTarget(AKamikazeCarrierDrone* Carrier) const;

	// ---- Pulse ----

	/** Four times a second while the siege runs: the trickle, the lull check, the carrier clock,
	 *  and the break log. */
	void Pulse();
	void TickSpawnQueue(float Now);
	void TickLull(float Now);
	void TickCarrierPacing(float DeltaTime);
	void TickBreakLog(float Now);

	/** Budget cost of every live enemy this director spawned. */
	float GetAliveCost() const;

	/** An authored wave's DelayBeforeWave, 0 for an endless one. */
	float GetExtraDelayBefore(int32 WaveNumber) const;

	float GetServerNow() const;

	/** Budget cost of one of this class: its EndlessPool entry, else 1. */
	float CostOf(TSubclassOf<AShooterNPC> NPCClass) const;
	float CostOfWave(const FArenaWave& Wave) const;

	void RefreshAliveCount();

	UFUNCTION()
	void OnEnemyDied(AShooterNPC* DeadNPC);

	UFUNCTION()
	void OnCoreChanged(ABuildableActor* Core);

	UFUNCTION()
	void OnStartTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	TArray<TWeakObjectPtr<AArenaSpawnPoint>> ResolvedSpawnPoints;
	TArray<TWeakObjectPtr<ABuildableActor>> ResolvedCores;
	TArray<TWeakObjectPtr<AShooterNPC>> Alive;
	FTimerHandle WaveTimer;
	int32 NextSpawnPointIndex = 0;

	/** Enemies of the waves so far that have not come out yet, oldest first. */
	TArray<TSubclassOf<AShooterNPC>> SpawnQueue;
	float NextTrickleTime = 0.0f;

	/** Where things were put down lately, so the next one keeps its distance. */
	struct FRecentSpawn
	{
		FVector Location = FVector::ZeroVector;
		float Time = 0.0f;
		bool bAir = false;
	};
	TArray<FRecentSpawn> RecentSpawns;

	FTimerHandle PulseTimer;
	float LastPulseTime = -1.0f;

	/** Salvos owed by the shared carrier clock, held at one while nobody is ready. */
	float PacedSalvoCredit = 0.0f;

	bool bBaseQuiet = false;
	float BaseQuietSince = 0.0f;

	bool bWarnedNoBaseNav = false;
};
