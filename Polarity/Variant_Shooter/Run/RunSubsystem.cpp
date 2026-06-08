// RunSubsystem.cpp

#include "RunSubsystem.h"

#include "Engine/World.h"
#include "Polarity/Upgrades/UpgradeManagerComponent.h"
#include "Polarity/Upgrades/UpgradeDefinition.h"

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
