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
class UInputAction;
class APlayerController;
struct FHintDisplayData;

// Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTutorialCompleted, FName, TutorialID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHintShown, FName, TutorialID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSlideShown, FName, TutorialID);

/**
 * Subsystem managing the tutorial/hint system.
 * Handles showing hints, fullscreen slides, tracking completion, and input icon lookup.
 */
UCLASS()
class POLARITY_API UTutorialSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	// ==================== Lifecycle ====================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

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

	/** Is a hint currently active */
	bool bHintActive = false;

	/** Is a slide currently active */
	bool bSlideActive = false;

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
