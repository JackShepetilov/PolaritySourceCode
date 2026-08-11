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
class UBiomeRunRegistry;
class UUserWidget;
class UWorld;
class UTexture2D;

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

	UFUNCTION(BlueprintCallable, Category = "Run|Generation")
	bool OpenNewRunFromBiome(UBiomeRunRegistry* BiomeRegistry,
		TSubclassOf<UUserWidget> LoadingScreenClass = nullptr,
		UTexture2D* LoadingSpinnerTexture = nullptr);

	/** Stops the cross-map loading screen after the player has actually been launched. */
	void DismissRunLoadingScreen();

	/** The class selected by the menu; GameMode reuses it as the post-load UMG cover. */
	TSubclassOf<UUserWidget> GetRunLoadingScreenClass() const { return RunLoadingScreenClass; }

	UFUNCTION(BlueprintCallable, Category = "Run")
	void EnterArena(int32 ArenaIndex);

	UFUNCTION(BlueprintCallable, Category = "Run")
	void ClearArena(int32 ArenaIndex);

	UFUNCTION(BlueprintPure, Category = "Run")
	bool IsArenaCleared(int32 ArenaIndex) const { return ClearedArenaIndices.Contains(ArenaIndex); }

	const TSet<int32>& GetClearedArenaIndices() const { return ClearedArenaIndices; }


	// ==================== State accessors ====================

	UFUNCTION(BlueprintPure, Category = "Run")
	ERunState GetRunState() const { return RunState; }

	UFUNCTION(BlueprintPure, Category = "Run")
	int32 GetCurrentArenaIndex() const { return CurrentArenaIndex; }

	UFUNCTION(BlueprintPure, Category = "Run")
	const FRunStats& GetStats() const { return Stats; }

	UFUNCTION(BlueprintPure, Category = "Run")
	bool IsRunActive() const { return RunState == ERunState::Active; }

	// ==================== Deterministic biome assembly ====================

	/** Returns the active run seed, creating one exactly once before a new run starts. */
	UFUNCTION(BlueprintCallable, Category = "Run|Generation")
	int32 GetOrCreateRunSeed();

	UFUNCTION(BlueprintPure, Category = "Run|Generation")
	int32 GetRunSeed() const { return RunSeed; }

	/** Reuses exact arena IDs when resuming/reopening the same biome layout. */
	bool GetSavedBiomeAssembly(FName BiomeId, FName LayoutId, TArray<FName>& OutArenaIds) const;

	/** Records both the seed and resolved IDs; IDs protect saves from later registry reordering. */
	void CommitBiomeAssembly(int32 InSeed, FName BiomeId, FName LayoutId, const TArray<FName>& ArenaIds);

	void GetCurrentBiomeAssembly(FName& OutBiomeId, FName& OutLayoutId, TArray<FName>& OutArenaIds) const
	{
		OutBiomeId = AssembledBiomeId;
		OutLayoutId = AssembledLayoutId;
		OutArenaIds = AssembledArenaIds;
	}

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

	// ==================== Save / restore (mid-run resume) ====================

	/** Restore the whole run-tier state from a save (mid-run resume). Sets the protected fields
	 *  directly, then fires OnAntennaCountChanged once so UI resyncs without N spurious increments. */
	void RestoreFromSave(ERunState InState, int32 InArenaIndex, int32 InActivatedAntennas,
		const TMap<FGameplayTag, int32>& InUpgrades, const FRunStats& InStats,
		int32 InRunSeed, FName InBiomeId, FName InLayoutId, const TArray<FName>& InArenaIds, const TArray<int32>& InClearedArenaIndices);

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
	TSet<int32> ClearedArenaIndices;


	UPROPERTY(SaveGame)
	FRunStats Stats;

	/** Run-wide count of activated antennas. SaveGame so it survives a mid-run save/load. */
	UPROPERTY(SaveGame)
	int32 ActivatedAntennaCount = 0;

	/** Run-scoped upgrade ledger (tag -> level). SaveGame so it survives a mid-run save/load
	 *  AND OpenLevel between biomes (this subsystem lives on the GameInstance). Reset in StartRun. */
	UPROPERTY(SaveGame)
	TMap<FGameplayTag, int32> AcquiredUpgrades;

	UPROPERTY(SaveGame)
	int32 RunSeed = 0;

	UPROPERTY(SaveGame)
	FName AssembledBiomeId;

	UPROPERTY(SaveGame)
	FName AssembledLayoutId;

	UPROPERTY(SaveGame)
	TArray<FName> AssembledArenaIds;

	/** Prevents two pre-StartRun maps/systems from generating different pending seeds. */
	bool bGenerationPreparedForPendingRun = false;

	double RunStartTimeSeconds = 0.0;

	/** Keeps the UMG object alive while MoviePlayer holds its Slate widget across OpenLevel. */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> RunLoadingScreenWidget;

	/** Texture retained for the native Slate spinner while its menu widget is unloaded. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> RunLoadingSpinnerTexture;

	TSubclassOf<UUserWidget> RunLoadingScreenClass;

	/** Re-attaches the editor/PIE fallback immediately after blocking map travel. */
	void HandlePostLoadMapForRunLoadingScreen(UWorld* LoadedWorld);

	FDelegateHandle PostLoadMapLoadingScreenHandle;
	bool bUsingViewportLoadingScreenFallback = false;
};
