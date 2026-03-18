// TutorialHintWidget.h
// Compact hint widget with input icon and text

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TutorialTypes.h"
#include "TutorialHintWidget.generated.h"

class UInputAction;
class UTexture2D;

/**
 * Display data for hint widget
 * Contains all resolved information needed for Blueprint display
 */
USTRUCT(BlueprintType)
struct FHintDisplayData
{
	GENERATED_BODY()

	/** Hint text to display */
	UPROPERTY(BlueprintReadOnly, Category = "Hint")
	FText HintText;

	/** Array of icons to display */
	UPROPERTY(BlueprintReadOnly, Category = "Hint")
	TArray<FTutorialInputIconData> Icons;

	/** If true, show "+" between icons (combination) */
	UPROPERTY(BlueprintReadOnly, Category = "Hint")
	bool bIsCombination = false;

	/** True if there are any valid icons to display */
	UPROPERTY(BlueprintReadOnly, Category = "Hint")
	bool bHasIcons = false;
};

/**
 * Single resolved entry for multi-hint display
 */
USTRUCT(BlueprintType)
struct FMultiHintEntryDisplayData
{
	GENERATED_BODY()

	/** Description text for this entry */
	UPROPERTY(BlueprintReadOnly, Category = "Hint")
	FText EntryText;

	/** Resolved icons for this entry */
	UPROPERTY(BlueprintReadOnly, Category = "Hint")
	TArray<FTutorialInputIconData> Icons;

	/** If true, show "+" between icons in this entry */
	UPROPERTY(BlueprintReadOnly, Category = "Hint")
	bool bIsCombination = false;

	/** True if there are valid icons in this entry */
	UPROPERTY(BlueprintReadOnly, Category = "Hint")
	bool bHasIcons = false;
};

/**
 * Display data for multi-hint widget (vertical list of entries)
 */
USTRUCT(BlueprintType)
struct FMultiHintDisplayData
{
	GENERATED_BODY()

	/** Array of resolved hint entries */
	UPROPERTY(BlueprintReadOnly, Category = "Hint")
	TArray<FMultiHintEntryDisplayData> Entries;
};

/**
 * Base widget class for displaying compact tutorial hints.
 * Shows an input icon and localized text.
 * Derive from this class in Blueprint to implement the visual design.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UTutorialHintWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	// ==================== Setup ====================

	/**
	 * Configure the hint with content (NEW - array version)
	 * @param InDisplayData - All data needed for hint display
	 * @param InInputActions - Original input actions (for completion detection)
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
	void SetupHintEx(const FHintDisplayData& InDisplayData, const TArray<UInputAction*>& InInputActions);

	/**
	 * Configure the hint with multi-entry content (vertical layout)
	 * @param InMultiDisplayData - All entries for vertical display
	 * @param InPrimaryInputAction - Primary input action for completion detection (can be null)
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
	void SetupMultiHint(const FMultiHintDisplayData& InMultiDisplayData, UInputAction* InPrimaryInputAction);

	/**
	 * DEPRECATED: Configure the hint with single icon
	 * Kept for backward compatibility
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint",
			  meta = (DeprecatedFunction, DeprecationMessage = "Use SetupHintEx instead"))
	void SetupHint(const FText& InText, UTexture2D* InIcon, UInputAction* InInputAction);

	/**
	 * Hide the hint with animation
	 * Called from TutorialSubsystem
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
	void HideHint();

	// ==================== Blueprint Events ====================

	/**
	 * Called when hint content is set (NEW - full data version)
	 * @param InDisplayData - Complete display data for Blueprint
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial|Hint",
			  meta = (DisplayName = "On Hint Setup Extended"))
	void BP_OnHintSetupEx(const FHintDisplayData& InDisplayData);

	/**
	 * Called when multi-hint content is set (vertical layout)
	 * @param InMultiDisplayData - All entries for vertical display
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial|Hint",
			  meta = (DisplayName = "On Multi Hint Setup"))
	void BP_OnMultiHintSetup(const FMultiHintDisplayData& InMultiDisplayData);

	/**
	 * DEPRECATED: Called when hint content is set (single icon version)
	 * Implement BP_OnHintSetupEx instead for new functionality
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial|Hint",
			  meta = (DisplayName = "On Hint Setup (Legacy)"))
	void BP_OnHintSetup(const FText& InHintText, UTexture2D* InKeyIcon);

	/**
	 * Called when hint should be hidden
	 * Implement in Blueprint to play hide animation, then call OnHideAnimationFinished
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial|Hint", meta = (DisplayName = "On Hide Hint"))
	void BP_OnHideHint();

	/**
	 * Call this from Blueprint when hide animation finishes
	 * Will remove the widget from parent
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
	void OnHideAnimationFinished();

	// ==================== Accessors ====================

	/** Get the primary input action (first in array) */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint")
	UInputAction* GetInputAction() const;

	/** Get all input actions */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint")
	TArray<UInputAction*> GetInputActions() const { return InputActions; }

	/** Get the hint text */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint")
	FText GetHintText() const { return DisplayData.HintText; }

	/** Get the display data */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint")
	FHintDisplayData GetDisplayData() const { return DisplayData; }

	/** Check if this hint has any icons */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint")
	bool HasIcons() const { return DisplayData.bHasIcons; }

	/** Check if this is a key combination (shows "+") */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint")
	bool IsCombination() const { return DisplayData.bIsCombination; }

	/** Check if this is a multi-hint */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint")
	bool IsMultiHint() const { return bIsMultiHint; }

	/** Get the multi-hint display data */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint")
	FMultiHintDisplayData GetMultiHintDisplayData() const { return MultiDisplayData; }

	/** DEPRECATED: Get the key icon (returns first icon) */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint",
			  meta = (DeprecatedFunction))
	UTexture2D* GetKeyIcon() const;

protected:

	/** Full display data (single hint mode) */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint")
	FHintDisplayData DisplayData;

	/** Multi-hint display data (multi hint mode) */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint")
	FMultiHintDisplayData MultiDisplayData;

	/** True if this is a multi-hint */
	bool bIsMultiHint = false;

	/** All associated input actions */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint")
	TArray<TObjectPtr<UInputAction>> InputActions;

	/** Is widget being hidden */
	bool bIsHiding = false;

	// ==================== Deprecated - Backward Compatibility ====================

	/** DEPRECATED: Current hint text (use DisplayData.HintText) */
	UPROPERTY()
	FText HintText;

	/** DEPRECATED: Current key icon (use DisplayData.Icons[0]) */
	UPROPERTY()
	TObjectPtr<UTexture2D> KeyIcon;

	/** DEPRECATED: Associated input action (use InputActions[0]) */
	UPROPERTY()
	TObjectPtr<UInputAction> InputAction;
};
