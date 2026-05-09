// RunSubsystem.h
// GameInstance subsystem coordinating roguelite run lifecycle.
// Per-run sub-systems (XP, run-scoped upgrades, run stats) subscribe here
// and reset their state on OnRunStarted.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ShooterNPC.h"
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

	// TODO Этап 3: TArray<FAppliedUpgrade> AppliedUpgrades — список апгрейдов,
	// переживающий смену арены, реаплаится на нового Character при спавне.

protected:
	UPROPERTY(SaveGame)
	ERunState RunState = ERunState::None;

	UPROPERTY(SaveGame)
	int32 CurrentArenaIndex = -1;

	UPROPERTY(SaveGame)
	FRunStats Stats;

	double RunStartTimeSeconds = 0.0;
};
