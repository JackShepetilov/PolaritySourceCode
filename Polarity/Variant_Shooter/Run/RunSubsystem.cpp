// RunSubsystem.cpp

#include "RunSubsystem.h"

#include "Engine/World.h"

void URunSubsystem::StartRun()
{
	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] StartRun (was %d)"), (int32)RunState);

	RunState = ERunState::Active;
	CurrentArenaIndex = -1;
	Stats = FRunStats();

	if (UWorld* World = GetWorld())
	{
		RunStartTimeSeconds = World->GetTimeSeconds();
	}

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
