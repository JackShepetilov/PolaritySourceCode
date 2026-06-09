// RunSubsystem.cpp

#include "RunSubsystem.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Polarity/Upgrades/UpgradeManagerComponent.h"
#include "Polarity/Upgrades/UpgradeDefinition.h"
#include "Save/SaveGameSubsystem.h"

namespace
{
	USaveGameSubsystem* GetSaveSubsystem(const URunSubsystem* Self)
	{
		if (UGameInstance* GI = Self ? Self->GetGameInstance() : nullptr)
		{
			return GI->GetSubsystem<USaveGameSubsystem>();
		}
		return nullptr;
	}
}

void URunSubsystem::StartRun()
{
	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] StartRun (was %d)"), (int32)RunState);

	RunState = ERunState::Active;
	CurrentArenaIndex = -1;
	Stats = FRunStats();
	ActivatedAntennaCount = 0;
	AcquiredUpgrades.Reset();

	if (UWorld* World = GetWorld())
	{
		RunStartTimeSeconds = World->GetTimeSeconds();
	}

	// Reset any "3/5"-style UI back to zero for the new run.
	OnAntennaCountChanged.Broadcast(ActivatedAntennaCount);

	OnRunStarted.Broadcast();

	// New run supersedes any stale mid-run resume; it re-checkpoints on the first EnterArena.
	if (USaveGameSubsystem* Save = GetSaveSubsystem(this))
	{
		Save->ClearRun();
	}
}

void URunSubsystem::EndRun(ERunEndReason Reason)
{
	if (RunState == ERunState::Ended || RunState == ERunState::None)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		Stats.RunDuration = static_cast<float>(World->GetTimeSeconds() - RunStartTimeSeconds);
	}

	RunState = ERunState::Ended;

	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] EndRun reason=%d duration=%.1fs xp=%d levels=%d"),
		(int32)Reason, Stats.RunDuration, Stats.TotalXPEarned, Stats.LevelsGained);

	OnRunEnded.Broadcast(Reason);

	// Persist banked meta (Stream's HandleRunEnded ran synchronously during the broadcast above),
	// then either keep the run save for resume (quit) or delete it (death / victory / abort).
	if (USaveGameSubsystem* Save = GetSaveSubsystem(this))
	{
		Save->SaveMetaNow();
		if (Reason == ERunEndReason::QuitToMenu)
		{
			Save->SaveRun();
		}
		else
		{
			Save->ClearRun();
		}
	}
}

void URunSubsystem::EnterArena(int32 ArenaIndex)
{
	if (RunState != ERunState::Active)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RUN_DEBUG] EnterArena %d called while RunState=%d (ignored)"),
			ArenaIndex, (int32)RunState);
		return;
	}
	CurrentArenaIndex = ArenaIndex;
	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] EnterArena %d"), ArenaIndex);
	OnArenaEntered.Broadcast(ArenaIndex);

	// Checkpoint the run so a quit-to-menu from this arena can resume here.
	if (USaveGameSubsystem* Save = GetSaveSubsystem(this))
	{
		Save->SaveRun();
	}
}

void URunSubsystem::ClearArena(int32 ArenaIndex)
{
	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] ClearArena %d"), ArenaIndex);
	OnArenaCleared.Broadcast(ArenaIndex);
}

void URunSubsystem::RegisterAntennaActivated()
{
	++ActivatedAntennaCount;
	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] RegisterAntennaActivated — count now %d"), ActivatedAntennaCount);
	OnAntennaCountChanged.Broadcast(ActivatedAntennaCount);
}

void URunSubsystem::AddXPEarnedToStats(int32 Amount)
{
	Stats.TotalXPEarned += Amount;
}

void URunSubsystem::AddLevelGainedToStats()
{
	++Stats.LevelsGained;
}

void URunSubsystem::RegisterKillInStats(TSubclassOf<AShooterNPC> EnemyClass)
{
	if (!EnemyClass) return;
	int32& Count = Stats.KillsByEnemy.FindOrAdd(EnemyClass);
	++Count;
}

void URunSubsystem::BindUpgradeManager(UUpgradeManagerComponent* Manager, const UUpgradeRegistry* Registry)
{
	if (!Manager)
	{
		return;
	}

	// 1) Re-apply the persisted ledger onto this fresh character FIRST — before subscribing.
	//    If we subscribed first, GrantUpgrade's broadcasts would re-enter our handlers and
	//    mutate AcquiredUpgrades while RestoreUpgrades iterates it. Only mid-run; outside a run
	//    the ledger may be stale until the next StartRun, so we don't reapply it.
	if (IsRunActive() && Registry)
	{
		UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] BindUpgradeManager: restoring %d upgrade(s) onto new character"),
			AcquiredUpgrades.Num());
		Manager->RestoreUpgrades(AcquiredUpgrades, Registry);
	}

	// 2) Subscribe so future grants/level-ups/removals keep the ledger current.
	//    AddUnique → safe to call once per character spawned during the run.
	Manager->OnUpgradeGranted.AddUniqueDynamic(this, &URunSubsystem::HandleUpgradeGranted);
	Manager->OnUpgradeLeveledUp.AddUniqueDynamic(this, &URunSubsystem::HandleUpgradeLeveledUp);
	Manager->OnUpgradeRemoved.AddUniqueDynamic(this, &URunSubsystem::HandleUpgradeRemoved);
}

void URunSubsystem::HandleUpgradeGranted(UUpgradeDefinition* Definition)
{
	if (Definition && Definition->UpgradeTag.IsValid())
	{
		AcquiredUpgrades.Add(Definition->UpgradeTag, 1);
	}
}

void URunSubsystem::HandleUpgradeLeveledUp(UUpgradeDefinition* Definition, int32 NewLevel)
{
	if (Definition && Definition->UpgradeTag.IsValid())
	{
		AcquiredUpgrades.Add(Definition->UpgradeTag, NewLevel);
	}
}

void URunSubsystem::HandleUpgradeRemoved(UUpgradeDefinition* Definition)
{
	if (Definition && Definition->UpgradeTag.IsValid())
	{
		AcquiredUpgrades.Remove(Definition->UpgradeTag);
	}
}

void URunSubsystem::RestoreFromSave(ERunState InState, int32 InArenaIndex, int32 InActivatedAntennas,
	const TMap<FGameplayTag, int32>& InUpgrades, const FRunStats& InStats)
{
	RunState              = InState;
	CurrentArenaIndex     = InArenaIndex;
	ActivatedAntennaCount = InActivatedAntennas;
	AcquiredUpgrades      = InUpgrades;
	Stats                 = InStats;

	// Preserve elapsed run time across the resume (RunDuration was captured at save time).
	if (UWorld* World = GetWorld())
	{
		RunStartTimeSeconds = World->GetTimeSeconds() - Stats.RunDuration;
	}

	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] RestoreFromSave state=%d arena=%d antennas=%d upgrades=%d"),
		(int32)RunState, CurrentArenaIndex, ActivatedAntennaCount, AcquiredUpgrades.Num());

	// Resync UI once (don't replay N antenna increments).
	OnAntennaCountChanged.Broadcast(ActivatedAntennaCount);
}
