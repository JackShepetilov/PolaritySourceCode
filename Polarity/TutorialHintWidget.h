// TutorialHintWidget.h
// Compact hint widget with input icon and text

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TutorialHintWidget.generated.h"

class UInputAction;
class UTexture2D;

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
	 * Configure the hint with content
	 * Called from TutorialSubsystem before adding to viewport
	 * @param InText - Localized hint text
	 * @param InIcon - Icon texture for the input key
	 * @param InInputAction - Associated input action (for completion detection)
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
	void SetupHint(const FText& InText, UTexture2D* InIcon, UInputAction* InInputAction);

	/**
	 * Hide the hint with animation
	 * Called from TutorialSubsystem
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
	void HideHint();

	// ==================== Blueprint Events ====================

	/**
	 * Called when hint content is set
	 * Implement in Blueprint to update UI elements
	 * @param InHintText - Text to display
	 * @param InKeyIcon - Icon texture for the key
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial|Hint", meta = (DisplayName = "On Hint Setup"))
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

	/** Get the associated input action */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint")
	UInputAction* GetInputAction() const { return InputAction; }

	/** Get the hint text */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint")
	FText GetHintText() const { return HintText; }

	/** Get the key icon */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Hint")
	UTexture2D* GetKeyIcon() const { return KeyIcon; }

protected:

	/** Current hint text */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint")
	FText HintText;

	/** Current key icon */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint")
	TObjectPtr<UTexture2D> KeyIcon;

	/** Associated input action */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint")
	TObjectPtr<UInputAction> InputAction;

	/** Is widget being hidden */
	bool bIsHiding = false;
};