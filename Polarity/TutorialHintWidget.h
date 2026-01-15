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

	/** DEPRECATED: Get the key icon (returns first icon) */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint",
			  meta = (DeprecatedFunction))
	UTexture2D* GetKeyIcon() const;

protected:

	/** Full display data */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint")
	FHintDisplayData DisplayData;

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