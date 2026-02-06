// SubtitleSubsystem.h
// Subtitle management subsystem with queue support

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SubtitleTypes.h"
#include "SubtitleSubsystem.generated.h"

class USubtitleDataAsset;
class USubtitleWidget;

// ==================== Delegates ====================

/** Fired when a subtitle starts displaying */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSubtitleStarted, const FText&, Text, float, Duration);

/** Fired when a subtitle finishes displaying */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSubtitleFinished);

/** Fired when the entire queue is empty (all subtitles finished) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSubtitleQueueEmpty);

/**
 * Subsystem for managing subtitle display.
 *
 * Features:
 * - Queue system: subtitles play in sequence, waiting for previous to finish
 * - DataAsset integration: reference subtitles by ID from configured assets
 * - Duration from Sound: automatically calculates duration from sound asset (without playing it)
 * - Direct API: show subtitles without DataAsset via ShowSubtitleDirect()
 *
 * Usage from Blueprint:
 * 1. Get SubtitleSubsystem from GameInstance
 * 2. Call SetWidgetClass() with your UMG widget class (once, e.g., in GameMode)
 * 3. Call ShowSubtitle(DataAsset, "entry_id") or ShowSubtitleDirect(Text, Duration)
 * 4. The subsystem handles queuing and timing automatically
 */
UCLASS()
class POLARITY_API USubtitleSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	// ==================== Lifecycle ====================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ==================== Configuration ====================

	/**
	 * Set the widget class for subtitles.
	 * Must be called before showing any subtitles.
	 * @param InWidgetClass - Widget class (must inherit from USubtitleWidget)
	 */
	UFUNCTION(BlueprintCallable, Category = "Subtitle")
	void SetWidgetClass(TSubclassOf<USubtitleWidget> InWidgetClass);

	// ==================== Main API ====================

	/**
	 * Queue a subtitle from a DataAsset by ID.
	 * If a subtitle is currently playing, this will queue and play after.
	 * @param DataAsset - The subtitle data asset
	 * @param EntryID - The entry ID to display
	 * @return True if entry was found and queued
	 */
	UFUNCTION(BlueprintCallable, Category = "Subtitle")
	bool ShowSubtitle(USubtitleDataAsset* DataAsset, FName EntryID);

	/**
	 * Queue a subtitle from DataAsset WITH 2D sound playback.
	 * Sound is played when the subtitle starts displaying.
	 * If another subtitle is playing, this will queue and play after.
	 * @param DataAsset - The subtitle data asset
	 * @param EntryID - The entry ID to display
	 * @return True if entry was found and queued
	 */
	UFUNCTION(BlueprintCallable, Category = "Subtitle")
	bool ShowSubtitleWithSound(USubtitleDataAsset* DataAsset, FName EntryID);

	/**
	 * Queue a subtitle directly without DataAsset.
	 * @param Text - Text to display
	 * @param Duration - How long to show (seconds)
	 * @param Speaker - Optional speaker name
	 */
	UFUNCTION(BlueprintCallable, Category = "Subtitle")
	void ShowSubtitleDirect(FText Text, float Duration, FText Speaker = FText());

	/**
	 * Immediately hide the current subtitle and clear the queue.
	 */
	UFUNCTION(BlueprintCallable, Category = "Subtitle")
	void HideAllSubtitles();

	/**
	 * Skip the current subtitle and proceed to next in queue (if any).
	 */
	UFUNCTION(BlueprintCallable, Category = "Subtitle")
	void SkipCurrentSubtitle();

	// ==================== State Queries ====================

	/**
	 * Check if a subtitle is currently being displayed.
	 */
	UFUNCTION(BlueprintPure, Category = "Subtitle")
	bool IsSubtitleActive() const { return bSubtitleActive; }

	/**
	 * Get the number of subtitles waiting in queue.
	 */
	UFUNCTION(BlueprintPure, Category = "Subtitle")
	int32 GetQueueLength() const { return SubtitleQueue.Num(); }

	/**
	 * Check if subtitle system is properly configured.
	 */
	UFUNCTION(BlueprintPure, Category = "Subtitle")
	bool IsConfigured() const { return SubtitleWidgetClass != nullptr; }

	// ==================== Events ====================

	/** Fired when a subtitle starts displaying */
	UPROPERTY(BlueprintAssignable, Category = "Subtitle|Events")
	FOnSubtitleStarted OnSubtitleStarted;

	/** Fired when a subtitle finishes displaying */
	UPROPERTY(BlueprintAssignable, Category = "Subtitle|Events")
	FOnSubtitleFinished OnSubtitleFinished;

	/** Fired when the queue becomes empty (all subtitles finished) */
	UPROPERTY(BlueprintAssignable, Category = "Subtitle|Events")
	FOnSubtitleQueueEmpty OnSubtitleQueueEmpty;

protected:

	// ==================== Configuration ====================

	/** Widget class for subtitle display */
	UPROPERTY()
	TSubclassOf<USubtitleWidget> SubtitleWidgetClass;

	// ==================== State ====================

	/** Active subtitle widget instance */
	UPROPERTY()
	TObjectPtr<USubtitleWidget> ActiveWidget;

	/** Queue of pending subtitles */
	TArray<FSubtitleRequest> SubtitleQueue;

	/** Is a subtitle currently active */
	bool bSubtitleActive = false;

	/** Timer handle for subtitle duration */
	FTimerHandle SubtitleTimerHandle;

	// ==================== Internal ====================

	/**
	 * Process the next subtitle in queue.
	 * Called when current subtitle finishes or queue is modified.
	 */
	void ProcessQueue();

	/**
	 * Display a subtitle immediately (internal).
	 * @param Request - The subtitle request to display
	 */
	void DisplaySubtitle(const FSubtitleRequest& Request);

	/**
	 * Called when current subtitle duration expires.
	 */
	void OnSubtitleTimerExpired();

	/**
	 * Create and show the widget if not already created.
	 * @return The widget instance, or nullptr if configuration invalid
	 */
	USubtitleWidget* EnsureWidgetCreated();

	/**
	 * Get appropriate player controller for widget creation.
	 */
	APlayerController* GetPlayerController() const;
};
