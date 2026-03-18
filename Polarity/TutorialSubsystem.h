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
class UShooterBulletCounterUI;
class UInputAction;
class APlayerController;
struct FHintDisplayData;
struct FMultiHintDisplayData;

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
	virtual bool IsTickable() const override { return bHUDArrowActive; }
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

	// ==================== Hint API ====================

	/**
	 * Show a hint with input icon and text
	 * @param TutorialID - Unique identifier for this tutorial
	 * @param HintData - Hint content data
	 * @param PlayerController - Controller to show hint for (uses first local player if null)
	 * @return True if hint was shown
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
	bool ShowHint(FName TutorialID, const FTutorialHintData& HintData, APlayerController* PlayerController = nullptr);

	/**
	 * Hide the currently displayed hint
	 * @param bMarkCompleted - If true, marks this tutorial as completed
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
	void HideHint(bool bMarkCompleted = true);

	/**
	 * Check if a hint is currently being displayed
	 */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint")
	bool IsHintActive() const { return bHintActive; }

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

	/** Currently active hint widget */
	UPROPERTY()
	TObjectPtr<UTutorialHintWidget> ActiveHintWidget;

	/** Currently active slide widget */
	UPROPERTY()
	TObjectPtr<UTutorialSlideWidget> ActiveSlideWidget;

	/** ID of currently active hint */
	FName ActiveHintID;

	/** ID of currently active slide */
	FName ActiveSlideID;

	/** ID of currently active HUD arrow */
	FName ActiveHUDArrowID;

	/** Is a hint currently active */
	bool bHintActive = false;

	/** Is a slide currently active */
	bool bSlideActive = false;

	/** Is a HUD arrow currently active */
	bool bHUDArrowActive = false;

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
};
