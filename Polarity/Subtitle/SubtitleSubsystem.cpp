// SubtitleSubsystem.cpp

#include "SubtitleSubsystem.h"
#include "SubtitleDataAsset.h"
#include "SubtitleWidget.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

void USubtitleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Subscribe to level transitions so we can reset widget state
	FWorldDelegates::OnWorldCleanup.AddUObject(this, &USubtitleSubsystem::OnWorldCleanup);
}

void USubtitleSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldCleanup.RemoveAll(this);

	HideAllSubtitles();

	if (ActiveWidget)
	{
		ActiveWidget->RemoveFromParent();
		ActiveWidget = nullptr;
	}

	Super::Deinitialize();
}

void USubtitleSubsystem::SetWidgetClass(TSubclassOf<USubtitleWidget> InWidgetClass)
{
	if (!InWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("SubtitleSubsystem: SetWidgetClass called with null class"));
		return;
	}

	// If widget class changed and we have an active widget, destroy it
	if (SubtitleWidgetClass != InWidgetClass && ActiveWidget)
	{
		ActiveWidget->RemoveFromParent();
		ActiveWidget = nullptr;
	}

	SubtitleWidgetClass = InWidgetClass;
}

bool USubtitleSubsystem::ShowSubtitle(USubtitleDataAsset* DataAsset, FName EntryID)
{
	if (!DataAsset)
	{
		UE_LOG(LogTemp, Warning, TEXT("SubtitleSubsystem: ShowSubtitle called with null DataAsset"));
		return false;
	}

	FSubtitleEntry Entry;
	if (!DataAsset->FindEntry(EntryID, Entry))
	{
		UE_LOG(LogTemp, Warning, TEXT("SubtitleSubsystem: Entry '%s' not found in DataAsset"), *EntryID.ToString());
		return false;
	}

	const float Duration = DataAsset->GetEntryDuration(EntryID);

	FSubtitleRequest Request(Entry.Text, Duration, Entry.Speaker);
	SubtitleQueue.Add(Request);

	// If nothing is playing, start immediately
	if (!bSubtitleActive)
	{
		ProcessQueue();
	}

	return true;
}

bool USubtitleSubsystem::ShowSubtitleWithSound(USubtitleDataAsset* DataAsset, FName EntryID)
{
	if (!DataAsset)
	{
		UE_LOG(LogTemp, Warning, TEXT("SubtitleSubsystem: ShowSubtitleWithSound called with null DataAsset"));
		return false;
	}

	FSubtitleEntry Entry;
	if (!DataAsset->FindEntry(EntryID, Entry))
	{
		UE_LOG(LogTemp, Warning, TEXT("SubtitleSubsystem: Entry '%s' not found in DataAsset"), *EntryID.ToString());
		return false;
	}

	const float Duration = DataAsset->GetEntryDuration(EntryID);

	// Load sound synchronously
	USoundBase* Sound = Entry.Sound.LoadSynchronous();

	FSubtitleRequest Request(Entry.Text, Duration, Entry.Speaker, Sound);
	SubtitleQueue.Add(Request);

	// If nothing is playing, start immediately
	if (!bSubtitleActive)
	{
		ProcessQueue();
	}

	return true;
}

void USubtitleSubsystem::ShowSubtitleDirect(FText Text, float Duration, FText Speaker)
{
	if (Text.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("SubtitleSubsystem: ShowSubtitleDirect called with empty text"));
		return;
	}

	if (Duration <= 0.0f)
	{
		// Estimate duration from text length
		const int32 TextLength = Text.ToString().Len();
		Duration = FMath::Max(2.0f, TextLength / 15.0f);
	}

	FSubtitleRequest Request(Text, Duration, Speaker);
	SubtitleQueue.Add(Request);

	// If nothing is playing, start immediately
	if (!bSubtitleActive)
	{
		ProcessQueue();
	}
}

void USubtitleSubsystem::HideAllSubtitles()
{
	// Clear the queue
	SubtitleQueue.Empty();

	// Stop current subtitle
	if (bSubtitleActive)
	{
		// Cancel timer
		if (UWorld* World = GetGameInstance()->GetWorld())
		{
			World->GetTimerManager().ClearTimer(SubtitleTimerHandle);
		}

		// Hide widget
		if (ActiveWidget)
		{
			ActiveWidget->HideSubtitle();
		}

		bSubtitleActive = false;
		OnSubtitleFinished.Broadcast();
	}

	OnSubtitleQueueEmpty.Broadcast();
}

void USubtitleSubsystem::SkipCurrentSubtitle()
{
	if (!bSubtitleActive)
	{
		return;
	}

	// Cancel current timer
	if (UWorld* World = GetGameInstance()->GetWorld())
	{
		World->GetTimerManager().ClearTimer(SubtitleTimerHandle);
	}

	// Hide current
	if (ActiveWidget)
	{
		ActiveWidget->HideSubtitle();
	}

	bSubtitleActive = false;
	OnSubtitleFinished.Broadcast();

	// Process next in queue
	ProcessQueue();
}

void USubtitleSubsystem::ProcessQueue()
{
	// Don't process if something is already playing
	if (bSubtitleActive)
	{
		return;
	}

	// Check if queue is empty
	if (SubtitleQueue.Num() == 0)
	{
		OnSubtitleQueueEmpty.Broadcast();
		return;
	}

	// Get next request
	FSubtitleRequest Request = SubtitleQueue[0];
	SubtitleQueue.RemoveAt(0);

	// Display it
	DisplaySubtitle(Request);
}

void USubtitleSubsystem::DisplaySubtitle(const FSubtitleRequest& Request)
{
	USubtitleWidget* Widget = EnsureWidgetCreated();
	if (!Widget)
	{
		UE_LOG(LogTemp, Error, TEXT("SubtitleSubsystem: Failed to create widget. Call SetWidgetClass first."));
		// Still try to process queue to avoid getting stuck
		ProcessQueue();
		return;
	}

	// Play 2D sound if provided
	if (Request.SoundToPlay)
	{
		UGameplayStatics::PlaySound2D(GetGameInstance()->GetWorld(), Request.SoundToPlay);
	}

	// Show the subtitle
	Widget->ShowSubtitle(Request.Text, Request.Speaker, Request.Duration);
	bSubtitleActive = true;

	// Broadcast event
	OnSubtitleStarted.Broadcast(Request.Text, Request.Duration);

	// Set timer for duration
	UWorld* World = GetGameInstance()->GetWorld();
	if (World)
	{
		World->GetTimerManager().SetTimer(
			SubtitleTimerHandle,
			this,
			&USubtitleSubsystem::OnSubtitleTimerExpired,
			Request.Duration,
			false
		);
	}
}

void USubtitleSubsystem::OnSubtitleTimerExpired()
{
	if (!bSubtitleActive)
	{
		return;
	}

	// Hide current subtitle
	if (ActiveWidget)
	{
		ActiveWidget->HideSubtitle();
	}

	bSubtitleActive = false;
	OnSubtitleFinished.Broadcast();

	// Process next in queue
	ProcessQueue();
}

USubtitleWidget* USubtitleSubsystem::EnsureWidgetCreated()
{
	if (ActiveWidget)
	{
		return ActiveWidget;
	}

	if (!SubtitleWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("SubtitleSubsystem: No widget class set. Call SetWidgetClass first."));
		return nullptr;
	}

	APlayerController* PC = GetPlayerController();
	if (!PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("SubtitleSubsystem: No player controller available"));
		return nullptr;
	}

	ActiveWidget = CreateWidget<USubtitleWidget>(PC, SubtitleWidgetClass);
	if (ActiveWidget)
	{
		ActiveWidget->AddToViewport(100); // High Z-order to be on top
	}

	return ActiveWidget;
}

APlayerController* USubtitleSubsystem::GetPlayerController() const
{
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return nullptr;
	}

	UWorld* World = GI->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	return World->GetFirstPlayerController();
}

void USubtitleSubsystem::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	// Only care about game/PIE worlds being cleaned up
	if (!World || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("SubtitleSubsystem: World cleanup - resetting widget state"));

	// Cancel any active timer (it's tied to the old world's TimerManager)
	if (bSubtitleActive)
	{
		World->GetTimerManager().ClearTimer(SubtitleTimerHandle);
		bSubtitleActive = false;
	}

	// Clear the queue
	SubtitleQueue.Empty();

	// Widget will be destroyed by the engine during world cleanup.
	// Just null our pointer so EnsureWidgetCreated() recreates it on the new level.
	ActiveWidget = nullptr;
}
