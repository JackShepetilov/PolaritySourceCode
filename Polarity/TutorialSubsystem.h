// TutorialSubsystem.h
// Tutorial management subsystem

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TutorialTypes.h"
#include "TutorialSubsystem.generated.h"

class UInputIconsDataAsset;
class UTutorialHintWidget;
class UTutorialSlideWidget;
class UReminderPanelWidget;
class UShooterBulletCounterUI;
class UInputAction;
class APlayerController;
struct FHintDisplayData;
struct FMultiHintDisplayData;
struct FReminderDisplayData;

// Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTutorialCompleted, FName, TutorialID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHintShown, FName, TutorialID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSlideShown, FName, TutorialID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHUDArrowShown, FName, TutorialID);

/**
 * Subsystem managing the tutorial/hint system.
 * Handles showing hints, fullscreen slides, HUD arrows, tracking completion, and input icon lookup.
 */
UCLASS()
class POLARITY_API UTutorialSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:

	// ==================== Lifecycle ====================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ==================== FTickableGameObject ====================

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UTutorialSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return bHUDArrowActive || bHasActiveHoldHints; }
	virtual bool IsTickableWhenPaused() const override { return true; }

	// ==================== Configuration ====================

	/**
	 * Set the input icons data asset for key-to-icon lookup
	 * Should be called early (e.g., from GameMode or PlayerController BeginPlay)
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void SetInputIconsAsset(UInputIconsDataAsset* InAsset);

	/**
	 * Set widget classes for hints and slides
	 * Must be called before showing any tutorials
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void SetWidgetClasses(TSubclassOf<UTutorialHintWidget> HintClass, TSubclassOf<UTutorialSlideWidget> SlideClass);

	/**
	 * Set the HUD widget for tutorial arrow display
	 * Must be called before showing HUD arrows (usually from HUD initialization)
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void SetHUDWidget(UShooterBulletCounterUI* InHUDWidget);

	/** Get the current HUD widget (may be null if not yet set) */
	UFUNCTION(BlueprintPure, Category = "Tutorial")
	UShooterBulletCounterUI* GetHUDWidget() const { return HUDWidget; }

	// ==================== Hint API ====================

	/**
	 * Show a hint with input icon and text.
	 * Supports multiple simultaneous hints (stacking). Duplicate TutorialIDs are rejected.
	 * @param TutorialID - Unique identifier for this tutorial
	 * @param HintData - Hint content data
	 * @param PlayerController - Controller to show hint for (uses first local player if null)
	 * @return True if hint was shown
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
	bool ShowHint(FName TutorialID, const FTutorialHintData& HintData, APlayerController* PlayerController = nullptr);

	/**
	 * Hide a specific hint by TutorialID
	 * @param TutorialID - ID of the hint to hide
	 * @param bMarkCompleted - If true, marks this tutorial as completed
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
	void HideHintByID(FName TutorialID, bool bMarkCompleted = true);

	/**
	 * Hide all currently displayed hints (backward compatible)
	 * @param bMarkCompleted - If true, marks all active hints as completed
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
	void HideHint(bool bMarkCompleted = true);

	/**
	 * Check if any hint is currently being displayed
	 */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint")
	bool IsHintActive() const { return ActiveHints.Num() > 0; }

	/**
	 * Check if a specific hint is currently being displayed
	 */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint")
	bool IsHintActiveByID(FName TutorialID) const;

	// ==================== Slide API ====================

	/**
	 * Show a fullscreen slide (pauses game)
	 * @param TutorialID - Unique identifier for this tutorial
	 * @param SlideData - Slide content data
	 * @param PlayerController - Controller to show slide for (uses first local player if null)
	 * @return True if slide was shown
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Slide")
	bool ShowSlide(FName TutorialID, const FTutorialSlideData& SlideData, APlayerController* PlayerController = nullptr);

	/**
	 * Close the currently displayed slide (unpauses game)
	 * @param bMarkCompleted - If true, marks this tutorial as completed
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Slide")
	void CloseSlide(bool bMarkCompleted = true);

	/**
	 * Check if a slide is currently being displayed
	 */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Slide")
	bool IsSlideActive() const { return bSlideActive; }

	// ==================== HUD Arrow API ====================

	/**
	 * Show a tutorial arrow pointing at a HUD element (pauses game, hold to close)
	 * @param TutorialID - Unique identifier for this tutorial
	 * @param ArrowData - Arrow configuration data
	 * @param PlayerController - Controller (uses first local player if null)
	 * @return True if arrow was shown
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|HUDArrow")
	bool ShowHUDArrow(FName TutorialID, const FTutorialHUDArrowData& ArrowData, APlayerController* PlayerController = nullptr);

	/**
	 * Close the currently displayed HUD arrow (unpauses game)
	 * @param bMarkCompleted - If true, marks this tutorial as completed
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|HUDArrow")
	void CloseHUDArrow(bool bMarkCompleted = true);

	/**
	 * Check if a HUD arrow is currently being displayed
	 */
	UFUNCTION(BlueprintPure, Category = "Tutorial|HUDArrow")
	bool IsHUDArrowActive() const { return bHUDArrowActive; }

	/**
	 * Debug mode: instantly show+hide arrows for all HUD elements and mark all given tutorials as completed.
	 * Does NOT pause the game. Used to reveal HUD elements that are hidden until their tutorial fires.
	 * @param TutorialIDsToComplete - Tutorial IDs to mark completed (arrows + weapon slides)
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Debug")
	void RunTutorialDebugReveal(const TArray<FName>& TutorialIDsToComplete);

	// ==================== Reminder Panel API ====================

	/**
	 * Set the widget class for the reminder panel
	 * Must be called before SetReminderData
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Reminder")
	void SetReminderWidgetClass(TSubclassOf<UReminderPanelWidget> InClass);

	/**
	 * Set the dismiss input action (shared with hold-hint dismiss).
	 * When held: dismisses active hold-hints (priority) or shows reminder panel.
	 * When released: hides reminder panel.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Reminder")
	void SetReminderDismissAction(UInputAction* InAction);

	/**
	 * Set reminder panel data. Each entry is a full FTutorialHintData.
	 * Creates the widget and resolves icons via InputAction pipeline.
	 * @param InData - Reminder entries (CompletionType/DismissAction/HoldDuration fields are ignored)
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Reminder")
	void SetReminderData(const FReminderPanelData& InData);

	/** Show the reminder panel */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Reminder")
	void ShowReminder();

	/** Hide the reminder panel */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Reminder")
	void HideReminder();

	/** Check if the reminder panel is currently visible */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Reminder")
	bool IsReminderVisible() const { return bReminderVisible; }

	// ==================== Completion Tracking ====================

	/**
	 * Mark a tutorial as completed
	 * @param TutorialID - Tutorial to mark
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Progress")
	void MarkCompleted(FName TutorialID);

	/**
	 * Check if a tutorial has been completed
	 * @param TutorialID - Tutorial to check
	 */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Progress")
	bool IsCompleted(FName TutorialID) const;

	/**
	 * Reset completion status for a tutorial
	 * @param TutorialID - Tutorial to reset
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Progress")
	void ResetCompletion(FName TutorialID);

	/**
	 * Reset all tutorial completion progress
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Progress")
	void ResetAllProgress();

	// ==================== Input Icon Lookup ====================

	/**
	 * Get icon texture for an input action based on current key bindings
	 * @param InputAction - The input action to look up
	 * @param PlayerController - Controller to query bindings from (uses first local player if null)
	 * @return Icon texture, or fallback if not found
	 */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Icons")
	UTexture2D* GetIconForInputAction(const UInputAction* InputAction, APlayerController* PlayerController = nullptr) const;

	/**
	 * Get icon texture for a specific key
	 * @param Key - The key to look up
	 * @return Icon texture, or fallback if not found
	 */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Icons")
	UTexture2D* GetIconForKey(const FKey& Key) const;

	/**
	 * Get the first bound key for an input action
	 * @param InputAction - The input action to query
	 * @param PlayerController - Controller to query bindings from
	 * @return The first bound key, or EKeys::Invalid if none
	 */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Icons")
	FKey GetFirstKeyForInputAction(const UInputAction* InputAction, APlayerController* PlayerController = nullptr) const;

	/**
	 * Get icons for multiple input actions
	 * @param InputActions - Array of input actions to look up
	 * @param PlayerController - Controller for key bindings
	 * @return Array of icon data (same order as input)
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Icons")
	TArray<FTutorialInputIconData> GetIconsForInputActions(const TArray<UInputAction*>& InputActions, APlayerController* PlayerController = nullptr) const;

	/**
	 * Build complete display data for hint
	 * @param HintData - Source hint data
	 * @param PlayerController - Controller for key bindings
	 * @return Ready-to-use display data
	 */
	FHintDisplayData BuildHintDisplayData(const FTutorialHintData& HintData, APlayerController* PlayerController) const;

	/**
	 * Build complete display data for multi-hint
	 * @param HintData - Source hint data with multi-hint entries
	 * @param PlayerController - Controller for key bindings
	 * @return Ready-to-use multi-hint display data
	 */
	FMultiHintDisplayData BuildMultiHintDisplayData(const FTutorialHintData& HintData, APlayerController* PlayerController) const;

	// ==================== Events ====================

	/** Fired when any tutorial is marked as completed */
	UPROPERTY(BlueprintAssignable, Category = "Tutorial|Events")
	FOnTutorialCompleted OnTutorialCompleted;

	/** Fired when a hint is shown */
	UPROPERTY(BlueprintAssignable, Category = "Tutorial|Events")
	FOnHintShown OnHintShown;

	/** Fired when a slide is shown */
	UPROPERTY(BlueprintAssignable, Category = "Tutorial|Events")
	FOnSlideShown OnSlideShown;

	/** Fired when a HUD arrow is shown */
	UPROPERTY(BlueprintAssignable, Category = "Tutorial|Events")
	FOnHUDArrowShown OnHUDArrowShown;

protected:

	// ==================== Configuration ====================

	/** Data asset for key-to-icon mapping */
	UPROPERTY()
	TObjectPtr<UInputIconsDataAsset> InputIconsAsset;

	/** Widget class for hints */
	UPROPERTY()
	TSubclassOf<UTutorialHintWidget> HintWidgetClass;

	/** Widget class for slides */
	UPROPERTY()
	TSubclassOf<UTutorialSlideWidget> SlideWidgetClass;

	/** HUD widget for tutorial arrows */
	UPROPERTY()
	TObjectPtr<UShooterBulletCounterUI> HUDWidget;

	// ==================== State ====================

	/** Set of completed tutorial IDs */
	TSet<FName> CompletedTutorials;

	// ---- Multi-hint stacking ----

	/** Tracked state for a single active hint */
	struct FActiveHintEntry
	{
		FName TutorialID;
		/** Original hint data (kept for rebuilding the combined widget) */
		FTutorialHintData HintData;
		// Hold-dismiss state (only for OnHoldInput hints)
		bool bIsHoldHint = false;
		float HoldDuration = 0.f;
		float CurrentHoldTime = 0.f;
		bool bIsHolding = false;
		FKey ExpectedDismissKey;
	};

	/** All currently active hints */
	TArray<FActiveHintEntry> ActiveHints;

	/** Single shared widget displaying all active hints */
	UPROPERTY()
	TObjectPtr<UTutorialHintWidget> ActiveHintWidget;

	/** True if any active hint is a hold-hint (for IsTickable) */
	bool bHasActiveHoldHints = false;

	// ---- Slide / HUD Arrow (unchanged, single-instance) ----

	/** Currently active slide widget */
	UPROPERTY()
	TObjectPtr<UTutorialSlideWidget> ActiveSlideWidget;

	/** ID of currently active slide */
	FName ActiveSlideID;

	/** ID of currently active HUD arrow */
	FName ActiveHUDArrowID;

	/** Is a slide currently active */
	bool bSlideActive = false;

	/** Is a HUD arrow currently active */
	bool bHUDArrowActive = false;

	// ---- Reminder Panel ----

	/** Widget class for reminder panel */
	UPROPERTY()
	TSubclassOf<UReminderPanelWidget> ReminderWidgetClass;

	/** Active reminder panel widget (created once, shown/hidden) */
	UPROPERTY()
	TObjectPtr<UReminderPanelWidget> ReminderWidget;

	/** Stored reminder data for re-resolving icons */
	FReminderPanelData StoredReminderData;

	/** Dismiss input action (shared: hold-hints + reminder toggle) */
	UPROPERTY()
	TObjectPtr<UInputAction> DismissAction;

	/** Is the reminder panel currently visible */
	bool bReminderVisible = false;

	/** Is the dismiss key currently held (for reminder show/hide) */
	bool bDismissKeyHeld = false;

	// ==================== HUD Arrow State ====================

	/** Currently active HUD arrow element */
	EHUDElement ActiveArrowElement = EHUDElement::ChargeBar;

	/** Expected close key for HUD arrow */
	FKey ArrowExpectedCloseKey;

	/** Hold duration required to close arrow */
	float ArrowHoldDuration = 1.0f;

	/** Current hold time for arrow close */
	float ArrowCurrentHoldTime = 0.0f;

	/** True while close key is held for arrow */
	bool bArrowHoldingCloseKey = false;

	/** Slate-level input processor for arrow key down/up */
	void BindArrowInput();
	void UnbindArrowInput();
	TSharedPtr<class FArrowInputProcessor> ArrowInputProcessor;

public:
	// Called by FArrowInputProcessor — not for external use
	/** Check if key is valid for closing HUD arrow */
	bool IsArrowCloseKey(const FKey& Key) const;
	/** True while close key is held */
	bool IsArrowHolding() const { return bArrowHoldingCloseKey; }
	/** Handle key down for arrow close */
	void HandleArrowKeyDown(const FKeyEvent& InKeyEvent);
	/** Handle key up for arrow close */
	void HandleArrowKeyUp(const FKeyEvent& InKeyEvent);

protected:

	// ==================== Internal ====================

	/** Get the appropriate player controller */
	APlayerController* GetPlayerController(APlayerController* Provided) const;

	/**
	 * Migrate deprecated single InputAction to array
	 * Called automatically during ShowHint
	 */
	static void MigrateHintDataIfNeeded(FTutorialHintData& HintData);

	/**
	 * Validate subsystem configuration
	 * @param OutError - Error message if validation fails
	 * @return True if configured correctly
	 */
	bool ValidateConfiguration(FString& OutError) const;

	/**
	 * Called when a world is being cleaned up (level transition).
	 * Resets widget state so widgets are recreated on the new level.
	 */
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	// ---- Dismiss Action Input Handling ----

	/** Bind dismiss action to Enhanced Input (called once from SetReminderDismissAction) */
	void BindDismissInput();

	/** Unbind dismiss action */
	void UnbindDismissInput();

	/** Called when dismiss action starts (key pressed) */
	void OnDismissActionStarted();

	/** Called when dismiss action completes (key released) */
	void OnDismissActionCompleted();

	/** Recalculate bHasActiveHoldHints flag */
	void UpdateHoldHintsFlag();

	/** Rebuild the single hint widget from all active hints */
	void RebuildHintWidget();

	/** Find active hint entry by ID (returns nullptr if not found) */
	FActiveHintEntry* FindActiveHint(FName TutorialID);
};
