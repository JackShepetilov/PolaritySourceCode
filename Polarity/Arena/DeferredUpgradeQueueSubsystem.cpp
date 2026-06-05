// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "DeferredUpgradeQueueSubsystem.h"
#include "Polarity/Variant_Shooter/XP/XPSubsystem.h"
#include "Engine/GameInstance.h"

void UDeferredUpgradeQueueSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Force XPSubsystem to initialize FIRST so its delegate exists when we bind to it.
	// Without this, GameInstance subsystem init order is undefined and the bind silently fails.
	Collection.InitializeDependency(UXPSubsystem::StaticClass());

	Super::Initialize(Collection);

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UXPSubsystem* XP = GI->GetSubsystem<UXPSubsystem>())
		{
			XP->OnLevelUp.AddDynamic(this, &UDeferredUpgradeQueueSubsystem::HandleLevelUp);
			CachedXPSubsystem = XP;
			UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] DeferredQueue::Initialize — bound to XPSubsystem::OnLevelUp"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[UPGRADE_DEBUG] DeferredQueue::Initialize — XPSubsystem STILL not found after InitializeDependency"));
		}
	}
}

void UDeferredUpgradeQueueSubsystem::Deinitialize()
{
	if (UXPSubsystem* XP = CachedXPSubsystem.Get())
	{
		XP->OnLevelUp.RemoveDynamic(this, &UDeferredUpgradeQueueSubsystem::HandleLevelUp);
	}
	CachedXPSubsystem.Reset();
	PendingLevelUps.Reset();
	bCapturing = false;

	Super::Deinitialize();
}

void UDeferredUpgradeQueueSubsystem::BeginCapture()
{
	if (bCapturing)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] DeferredQueue::BeginCapture — already capturing, no-op (pending=%d)"), PendingLevelUps.Num());
		return;
	}
	bCapturing = true;
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] DeferredQueue::BeginCapture — capture STARTED (pending=%d)"), PendingLevelUps.Num());
}

void UDeferredUpgradeQueueSubsystem::EndCapture()
{
	if (!bCapturing)
	{
		return;
	}
	bCapturing = false;
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] DeferredQueue::EndCapture — capture STOPPED (pending=%d, NOT flushed)"), PendingLevelUps.Num());
}

void UDeferredUpgradeQueueSubsystem::FlushAll()
{
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] DeferredQueue::FlushAll — releasing %d queued level-ups (bound listeners=%d)"),
		PendingLevelUps.Num(), OnDeferredLevelUpReleased.IsBound() ? 1 : 0);

	bCapturing = false;

	// Snapshot+clear before broadcasting so handlers can't accidentally re-enter the queue
	TArray<FDeferredLevelUp> ToRelease;
	Swap(ToRelease, PendingLevelUps);

	for (const FDeferredLevelUp& Entry : ToRelease)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] DeferredQueue::FlushAll — broadcasting lvl=%d"), Entry.NewLevel);
		OnDeferredLevelUpReleased.Broadcast(Entry.NewLevel);
	}
}

void UDeferredUpgradeQueueSubsystem::ClearWithoutReleasing()
{
	if (PendingLevelUps.Num() == 0 && !bCapturing)
	{
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] DeferredQueue::ClearWithoutReleasing — dropping %d pending"), PendingLevelUps.Num());
	PendingLevelUps.Reset();
	bCapturing = false;
}

void UDeferredUpgradeQueueSubsystem::HandleLevelUp(int32 NewLevel)
{
	if (bCapturing)
	{
		FDeferredLevelUp Entry;
		Entry.NewLevel = NewLevel;
		PendingLevelUps.Add(Entry);
		UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] DeferredQueue::HandleLevelUp — CAPTURED (lvl=%d, queue=%d)"),
			NewLevel, PendingLevelUps.Num());
		return;
	}

	// Pass-through — behave like a normal observer
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] DeferredQueue::HandleLevelUp — PASS-THROUGH (lvl=%d, listeners=%d)"),
		NewLevel, OnDeferredLevelUpReleased.IsBound() ? 1 : 0);
	OnDeferredLevelUpReleased.Broadcast(NewLevel);
}
