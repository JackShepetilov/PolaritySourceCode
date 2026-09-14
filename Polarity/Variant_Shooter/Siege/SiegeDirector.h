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
// Server only: it spawns and it counts. The few numbers a HUD wants are replicated.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Arena/ArenaWaveData.h"
#include "SiegeDirector.generated.h"

class AArenaSpawnPoint;
class ABuildableActor;
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

	// ==================== Spawn ====================

	/** Where the enemy enters. The same markers the arenas use, air spawn included. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn")
	TArray<TSoftObjectPtr<AArenaSpawnPoint>> SpawnPoints;

	/** Also take every AArenaSpawnPoint found in the world at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn")
	bool bAutoCollectSpawnPoints = true;

	// ==================== Start ====================

	/** Actors whose overlap by a player starts the siege (a console in the house). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Start")
	TArray<TSoftObjectPtr<AActor>> StartTriggers;

	/** Start the clock at BeginPlay instead of waiting for a trigger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Start")
	bool bStartOnBeginPlay = false;

	// ==================== Base ====================

	/** What the siege is after. Empty = every ASiegeCoreBuildable in the world at BeginPlay. When
	 *  the last of them falls the base is lost and the clock stops. */
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

	void ScheduleNextWave(float Delay);
	void StartWave();
	void SpawnAuthoredWave(const FArenaWave& Wave);
	void SpawnEndlessWave(int32 WaveNumber);
	void SpawnOne(TSubclassOf<AShooterNPC> NPCClass);
	AArenaSpawnPoint* PickSpawnPoint(TSubclassOf<AShooterNPC> NPCClass);

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
	TArray<TWeakObjectPtr<ASiegeCoreBuildable>> ResolvedCores;
	TArray<TWeakObjectPtr<AShooterNPC>> Alive;
	FTimerHandle WaveTimer;
	int32 NextSpawnPointIndex = 0;
};
