// RunSubsystem.h
// GameInstance subsystem coordinating roguelite run lifecycle.
// Per-run sub-systems (XP, run-scoped upgrades, run stats) subscribe here
// and reset their state on OnRunStarted.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ShooterNPC.h"
#include "GameplayTagContainer.h"
#include "RunSubsystem.generated.h"

UENUM(BlueprintType)
enum class ERunState : uint8
{
	None,
	Active,
	Paused,
	Ended
};

UENUM(BlueprintType)
enum class ERunEndReason : uint8
{
	PlayerDeath,
	Victory,
	QuitToMenu,
	Aborted
};

USTRUCT(BlueprintType)
struct FRunStats
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Run")
	int32 TotalXPEarned = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Run")
	int32 LevelsGained = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Run")
	float RunDuration = 0.f;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Run")
	TMap<TSubclassOf<AShooterNPC>, int32> KillsByEnemy;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRunStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRunEnded, ERunEndReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRunArenaEntered, int32, ArenaIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRunArenaCleared, int32, ArenaIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAntennaCountChanged, int32, NewCount);

class UUpgradeManagerComponent;
class UUpgradeRegistry;
class UUpgradeDefinition;

UCLASS()
class POLARITY_API URunSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ==================== Lifecycle API ====================

	UFUNCTION(BlueprintCallable, Category = "Run")
	void StartRun();

	UFUNCTION(BlueprintCallable, Category = "Run")
	void EndRun(ERunEndReason Reason);

	UFUNCTION(BlueprintCallable, Category = "Run")
	void EnterArena(int32 ArenaIndex);

	UFUNCTION(BlueprintCallable, Category = "Run")
	void ClearArena(int32 ArenaIndex);

	// ==================== State accessors ====================

	UFUNCTION(BlueprintPure, Category = "Run")
	ERunState GetRunState() const { return RunState; }

	UFUNCTION(BlueprintPure, Category = "Run")
	int32 GetCurrentArenaIndex() const { return CurrentArenaIndex; }

	UFUNCTION(BlueprintPure, Category = "Run")
	const FRunStats& GetStats() const { return Stats; }

	UFUNCTION(BlueprintPure, Category = "Run")
	bool IsRunActive() const { return RunState == ERunState::Active; }

	// ==================== Antennas (run-scoped objective progress) ====================

	/** Called once per antenna when it's activated (ArenaManager drives this on
	 *  HandleAntennaActivated). Bumps the run-wide count and fires OnAntennaCountChanged.
	 *  Each antenna can only activate once per run, so this is naturally de-duplicated. */
	UFUNCTION(BlueprintCallable, Category = "Run|Antennas")
	void RegisterAntennaActivated();

	/** Total antennas activated so far in the current run (resets in StartRun). */
	UFUNCTION(BlueprintPure, Category = "Run|Antennas")
	int32 GetActivatedAntennaCount() const { return ActivatedAntennaCount; }

	/** True once the player has activated at least N antennas this run. The threshold N
	 *  lives in the level (designer-set on the boss-path button), not in C++. */
	UFUNCTION(BlueprintPure, Category = "Run|Antennas")
	bool HasActivatedAtLeast(int32 N) const { return ActivatedAntennaCount >= N; }

	// ==================== Upgrades (run-scoped, carried across levels) ====================

	/** Called by the player character on spawn (BeginPlay). Re-applies the persisted upgrade
	 *  ledger onto the freshly-spawned character (only mid-run), then subscribes to the manager
	 *  so future grants/level-ups/removals keep the ledger current. The ledger lives here
	 *  (GameInstance subsystem) so it survives OpenLevel between biomes. */
	void BindUpgradeManager(UUpgradeManagerComponent* Manager, const UUpgradeRegistry* Registry);

	/** Read-only view of the persisted upgrades (tag -> level). */
	const TMap<FGameplayTag, int32>& GetUpgradeLedger() const { return AcquiredUpgrades; }

	// ==================== Stats aggregation (called by sub-systems) ====================

	void AddXPEarnedToStats(int32 Amount);
	void AddLevelGainedToStats();
	void RegisterKillInStats(TSubclassOf<AShooterNPC> EnemyClass);

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "Run|Events")
	FOnRunStarted OnRunStarted;

	UPROPERTY(BlueprintAssignable, Category = "Run|Events")
	FOnRunEnded OnRunEnded;

	UPROPERTY(BlueprintAssignable, Category = "Run|Events")
	FOnRunArenaEntered OnArenaEntered;

	UPROPERTY(BlueprintAssignable, Category = "Run|Events")
	FOnRunArenaCleared OnArenaCleared;

	/** Fires whenever ActivatedAntennaCount changes — UI (e.g. the hub "3/5" hologram)
	 *  binds to this to update live. Also fires with 0 on StartRun so listeners reset. */
	UPROPERTY(BlueprintAssignable, Category = "Run|Events")
	FOnAntennaCountChanged OnAntennaCountChanged;

protected:
	// --- Live-sync handlers: keep AcquiredUpgrades current as the player gains/loses upgrades ---
	UFUNCTION()
	void HandleUpgradeGranted(UUpgradeDefinition* Definition);

	UFUNCTION()
	void HandleUpgradeLeveledUp(UUpgradeDefinition* Definition, int32 NewLevel);

	UFUNCTION()
	void HandleUpgradeRemoved(UUpgradeDefinition* Definition);

	UPROPERTY(SaveGame)
	ERunState RunState = ERunState::None;

	UPROPERTY(SaveGame)
	int32 CurrentArenaIndex = -1;

	UPROPERTY(SaveGame)
	FRunStats Stats;

	/** Run-wide count of activated antennas. SaveGame so it survives a mid-run save/load. */
	UPROPERTY(SaveGame)
	int32 ActivatedAntennaCount = 0;

	/** Run-scoped upgrade ledger (tag -> level). SaveGame so it survives a mid-run save/load
	 *  AND OpenLevel between biomes (this subsystem lives on the GameInstance). Reset in StartRun. */
	UPROPERTY(SaveGame)
	TMap<FGameplayTag, int32> AcquiredUpgrades;

	double RunStartTimeSeconds = 0.0;
};
