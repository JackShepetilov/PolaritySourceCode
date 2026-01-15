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
	Slide
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
 * Data for a hint-type tutorial
 */
USTRUCT(BlueprintType)
struct FTutorialHintData
{
	GENERATED_BODY()

	/** Localized hint text to display */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hint")
	FText HintText;

	/** Input action associated with this hint (used for icon and completion) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hint")
	TObjectPtr<class UInputAction> InputAction;

	/** How this hint is completed/hidden */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hint")
	ETutorialCompletionType CompletionType = ETutorialCompletionType::OnInputAction;
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

	/** Optional: text hint for closing (e.g., "Press SPACE to continue") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide")
	FText CloseHintText;
};
