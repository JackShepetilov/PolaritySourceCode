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
#include "Engine/DataTable.h"

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

void USubtitleSubsystem::ShowSubtitleDirectWithSound(FText Text, float Duration, FText Speaker, USoundBase* Sound)
{
	if (Text.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("SubtitleSubsystem: ShowSubtitleDirectWithSound called with empty text"));
		return;
	}

	// Duration priority: DurationOverride > Sound duration > text estimate
	if (Duration <= 0.0f && Sound)
	{
		Duration = Sound->GetDuration();
	}
	if (Duration <= 0.0f)
	{
		const int32 TextLength = Text.ToString().Len();
		Duration = FMath::Max(2.0f, TextLength / 15.0f);
	}

	FSubtitleRequest Request(Text, Duration, Speaker, Sound);
	SubtitleQueue.Add(Request);

	if (!bSubtitleActive)
	{
		ProcessQueue();
	}
}

bool USubtitleSubsystem::ShowSubtitleFromTable(UDataTable* DataTable, FName RowName)
{
	if (!DataTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("SubtitleSubsystem: ShowSubtitleFromTable called with null DataTable"));
		return false;
	}

	// Search by ID field, not row name
	const FSubtitleEntry* Entry = nullptr;
	TArray<FName> AllRowNames = DataTable->GetRowNames();
	for (const FName& RN : AllRowNames)
	{
		const FSubtitleEntry* Row = DataTable->FindRow<FSubtitleEntry>(RN, TEXT("ShowSubtitleFromTable"));
		if (Row && Row->ID == RowName)
		{
			Entry = Row;
			break;
		}
	}

	if (!Entry)
	{
		UE_LOG(LogTemp, Warning, TEXT("SubtitleSubsystem: Entry with ID '%s' not found in DataTable"), *RowName.ToString());
		return false;
	}

	USoundBase* Sound = Entry->Sound.LoadSynchronous();

	float Duration = Entry->DurationOverride;
	if (Duration <= 0.0f && Sound)
	{
		Duration = Sound->GetDuration();
	}
	if (Duration <= 0.0f)
	{
		const int32 TextLength = Entry->Text.ToString().Len();
		Duration = FMath::Max(2.0f, TextLength / 15.0f);
	}

	FSubtitleRequest Request(Entry->Text, Duration, Entry->Speaker, Sound);
	SubtitleQueue.Add(Request);

	if (!bSubtitleActive)
	{
		ProcessQueue();
	}

	return true;
}

void USubtitleSubsystem::ShowSubtitleSequence(UDataTable* DataTable, const FString& Prefix)
{
	if (!DataTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("SubtitleSubsystem: ShowSubtitleSequence called with null DataTable"));
		return;
	}

	TArray<FName> RowNames = DataTable->GetRowNames();
	RowNames.Sort(FNameLexicalLess());

	UE_LOG(LogTemp, Warning, TEXT("SubtitleSubsystem: ShowSubtitleSequence called with prefix '%s', %d total rows"), *Prefix, RowNames.Num());

	// Collect matching entries sorted by ID
	TArray<TPair<FName, const FSubtitleEntry*>> MatchedEntries;

	int32 MatchCount = 0;
	for (const FName& RowName : RowNames)
	{
		const FSubtitleEntry* Entry = DataTable->FindRow<FSubtitleEntry>(RowName, TEXT("ShowSubtitleSequence"));
		if (!Entry)
		{
			continue;
		}

		if (!Entry->ID.ToString().StartsWith(Prefix))
		{
			continue;
		}
		MatchCount++;
		MatchedEntries.Add(TPair<FName, const FSubtitleEntry*>(Entry->ID, Entry));
	}

	// Sort by ID string so beach_01 comes before beach_02
	MatchedEntries.Sort([](const TPair<FName, const FSubtitleEntry*>& A, const TPair<FName, const FSubtitleEntry*>& B)
	{
		return A.Key.ToString() < B.Key.ToString();
	});

	for (const auto& Pair : MatchedEntries)
	{
		const FSubtitleEntry* Entry = Pair.Value;

		USoundBase* Sound = Entry->Sound.LoadSynchronous();

		float Duration = Entry->DurationOverride;
		if (Duration <= 0.0f && Sound)
		{
			Duration = Sound->GetDuration();
		}
		if (Duration <= 0.0f)
		{
			const int32 TextLength = Entry->Text.ToString().Len();
			Duration = FMath::Max(2.0f, TextLength / 15.0f);
		}

		FSubtitleRequest Request(Entry->Text, Duration, Entry->Speaker, Sound);
		SubtitleQueue.Add(Request);
	}

	UE_LOG(LogTemp, Warning, TEXT("SubtitleSubsystem: Matched %d rows with prefix '%s', queue size now %d, bSubtitleActive=%d"), MatchCount, *Prefix, SubtitleQueue.Num(), bSubtitleActive);

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
	UE_LOG(LogTemp, Warning, TEXT("SubtitleSubsystem: DisplaySubtitle - Text='%s', Speaker='%s', Duration=%.1f, WidgetClass=%s"),
		*Request.Text.ToString(), *Request.Speaker.ToString(), Request.Duration,
		SubtitleWidgetClass ? *SubtitleWidgetClass->GetName() : TEXT("NULL"));

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
