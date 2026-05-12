// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

// TODO (UI wiring, scaffolding-pass deferred):
//   UUpgradeChoiceWidget currently listens to UXPSubsystem::OnSkillLevelUp directly. For this
//   subsystem to actually suppress popups during capture, the widget should switch to listening
//   on OnDeferredLevelUpReleased instead (or guard its existing handler when capturing is on).
//   That's a 5-line change in UpgradeChoiceWidget.cpp; left out of this pass per "scaffolding first".

#include "DeferredUpgradeQueueSubsystem.h"
#include "Polarity/Variant_Shooter/XP/XPSubsystem.h"
#include "Engine/GameInstance.h"

void UDeferredUpgradeQueueSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UXPSubsystem* XP = GI->GetSubsystem<UXPSubsystem>())
		{
			XP->OnSkillLevelUp.AddDynamic(this, &UDeferredUpgradeQueueSubsystem::HandleSkillLevelUp);
			CachedXPSubsystem = XP;
			UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] DeferredQueue::Initialize — bound to XPSubsystem::OnSkillLevelUp"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[UPGRADE_DEBUG] DeferredQueue::Initialize — XPSubsystem not found"));
		}
	}
}

void UDeferredUpgradeQueueSubsystem::Deinitialize()
{
	if (UXPSubsystem* XP = CachedXPSubsystem.Get())
	{
		XP->OnSkillLevelUp.RemoveDynamic(this, &UDeferredUpgradeQueueSubsystem::HandleSkillLevelUp);
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
		UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] DeferredQueue::FlushAll — broadcasting cat=%d, lvl=%d"),
			(int32)Entry.Category, Entry.NewLevel);
		OnDeferredLevelUpReleased.Broadcast(Entry.Category, Entry.NewLevel);
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

void UDeferredUpgradeQueueSubsystem::HandleSkillLevelUp(ESkillCategory Category, int32 NewLevel)
{
	if (bCapturing)
	{
		FDeferredLevelUp Entry;
		Entry.Category = Category;
		Entry.NewLevel = NewLevel;
		PendingLevelUps.Add(Entry);
		UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] DeferredQueue::HandleSkillLevelUp — CAPTURED (cat=%d, lvl=%d, queue=%d)"),
			(int32)Category, NewLevel, PendingLevelUps.Num());
		return;
	}

	// Pass-through — behave like a normal observer
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] DeferredQueue::HandleSkillLevelUp — PASS-THROUGH (cat=%d, lvl=%d, listeners=%d)"),
		(int32)Category, NewLevel, OnDeferredLevelUpReleased.IsBound() ? 1 : 0);
	OnDeferredLevelUpReleased.Broadcast(Category, NewLevel);
}
