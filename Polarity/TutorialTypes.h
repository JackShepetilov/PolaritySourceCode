// TutorialTypes.h
// Base types for the tutorial system

#pragma once

#include "CoreMinimal.h"
#include "TutorialTypes.generated.h"

/**
 * Type of tutorial content to display
 */
UENUM(BlueprintType)
enum class ETutorialType : uint8
{
	/** Compact hint with input icon and text */
	Hint,
	/** Fullscreen slide with image (pauses game) */
	Slide,
	/** HUD arrow pointing to a specific HUD element (pauses game) */
	HUDArrow
};

/**
 * Tutorial completion condition for hints
 */
UENUM(BlueprintType)
enum class ETutorialCompletionType : uint8
{
	/** Completes when player performs the required input action */
	OnInputAction,
	/** Completes when player exits the trigger volume */
	OnExitVolume,
	/** Completes manually via Blueprint or C++ */
	Manual
};

/**
 * HUD elements that tutorial arrows can point to
 */
UENUM(BlueprintType)
enum class EHUDElement : uint8
{
	ChargeBar,
	HealthBar,
	MeleeCharges
};

/**
 * Single input action entry with resolved icon
 * Used for passing icon data to Blueprint
 */
USTRUCT(BlueprintType)
struct FTutorialInputIconData
{
	GENERATED_BODY()

	/** Resolved icon texture for this input */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial")
	TObjectPtr<class UTexture2D> Icon = nullptr;

	/** The key this icon represents (for debugging/display) */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial")
	FKey Key = EKeys::Invalid;

	/** Is this icon valid (has texture)? */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial")
	bool bIsValid = false;
};

/**
 * Single entry in a multi-hint (vertical list of action+description pairs)
 */
USTRUCT(BlueprintType)
struct FTutorialHintEntry
{
	GENERATED_BODY()

	/** Description text for this action */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hint")
	FText EntryText;

	/** Input actions for this entry's icon row */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hint")
	TArray<TObjectPtr<class UInputAction>> InputActions;

	/** If true, show "+" between icons in this row (combination) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hint")
	bool bIsCombination = false;
};

/**
 * Data for a hint-type tutorial
 */
USTRUCT(BlueprintType)
struct FTutorialHintData
{
	GENERATED_BODY()

	/** Localized hint text to display */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hint")
	FText HintText;

	/**
	 * Input actions associated with this hint
	 * Can be single action (E to interact) or multiple (WASD for movement)
	 * Empty array = text-only hint with no icons
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hint")
	TArray<TObjectPtr<class UInputAction>> InputActions;

	/**
	 * If true, show "+" separator between icons (for key combinations like Ctrl+E)
	 * If false, show icons side by side without separator (for alternatives like WASD)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hint")
	bool bIsCombination = false;

	/** How this hint is completed/hidden */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hint")
	ETutorialCompletionType CompletionType = ETutorialCompletionType::OnInputAction;

	// ==================== Multi-Hint Mode ====================

	/**
	 * If true, this hint uses vertical multi-entry layout instead of single hint.
	 * When enabled, MultiHintEntries is used instead of HintText/InputActions.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hint|Multi")
	bool bIsMultiHint = false;

	/**
	 * Entries for multi-hint mode (vertical list of action+description pairs).
	 * Only used when bIsMultiHint is true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hint|Multi",
		meta = (EditCondition = "bIsMultiHint", EditConditionHides))
	TArray<FTutorialHintEntry> MultiHintEntries;

	// ==================== Backward Compatibility ====================

	/**
	 * DEPRECATED: Use InputActions array instead
	 * Kept for backward compatibility - will be migrated to InputActions[0]
	 */
	UPROPERTY()
	TObjectPtr<class UInputAction> InputAction_DEPRECATED;

	/** Helper to get first input action (for completion detection) */
	class UInputAction* GetPrimaryInputAction() const
	{
		return InputActions.Num() > 0 ? InputActions[0].Get() : nullptr;
	}

	/** Check if any input actions are defined */
	bool HasInputActions() const
	{
		return InputActions.Num() > 0;
	}
};

/**
 * Data for a slide-type tutorial
 */
USTRUCT(BlueprintType)
struct FTutorialSlideData
{
	GENERATED_BODY()

	/** Fullscreen image to display */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide")
	TObjectPtr<class UTexture2D> SlideImage;

	/** Input action to close the slide (usually IA_Confirm or similar) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide")
	TObjectPtr<class UInputAction> CloseAction;

	/** Optional: text hint for closing (e.g., "Hold SPACE to continue") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide")
	FText CloseHintText;

	/** Duration in seconds the close button must be held to dismiss (0 = instant press) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float HoldDuration = 1.0f;
};

/**
 * Data for a HUD arrow tutorial (pauses game, shows arrows on HUD elements)
 */
USTRUCT(BlueprintType)
struct FTutorialHUDArrowData
{
	GENERATED_BODY()

	/** Which HUD element to point the arrow at */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD Arrow")
	EHUDElement TargetElement = EHUDElement::ChargeBar;

	/** Description text shown alongside the arrow */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD Arrow")
	FText DescriptionText;

	/** Input action to close (defaults to space/enter) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD Arrow")
	TObjectPtr<class UInputAction> CloseAction;

	/** Optional: text hint for closing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD Arrow")
	FText CloseHintText;

	/** Duration in seconds the close button must be held to dismiss */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD Arrow", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float HoldDuration = 1.0f;
};
