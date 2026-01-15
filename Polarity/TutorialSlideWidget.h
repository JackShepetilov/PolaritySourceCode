// TutorialSlideWidget.h
// Fullscreen slide widget for tutorial images

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TutorialSlideWidget.generated.h"

class UInputAction;
class UTexture2D;
class UTutorialSubsystem;

/**
 * Base widget class for displaying fullscreen tutorial slides.
 * Pauses the game and shows an image with close prompt.
 * Derive from this class in Blueprint to implement the visual design.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UTutorialSlideWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	// ==================== Setup ====================

	/**
	 * Configure the slide with content
	 * Called from TutorialSubsystem before adding to viewport
	 * @param InImage - Fullscreen slide image
	 * @param InCloseText - Text hint for closing (e.g., "Press SPACE to continue")
	 * @param InCloseIcon - Icon for the close key
	 * @param InCloseAction - Input action that closes the slide
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Slide")
	void SetupSlide(UTexture2D* InImage, const FText& InCloseText, UTexture2D* InCloseIcon, UInputAction* InCloseAction);

	/**
	 * Hide the slide with animation
	 * Called from TutorialSubsystem
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Slide")
	void HideSlide();

	/**
	 * Request to close the slide (triggers subsystem to close)
	 * Can be called from Blueprint button or input handling
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Slide")
	void RequestClose();

	// ==================== Blueprint Events ====================

	/**
	 * Called when slide content is set
	 * Implement in Blueprint to update UI elements
	 * @param InSlideImage - Image to display
	 * @param InCloseHintText - Text for close prompt
	 * @param InCloseKeyIcon - Icon for close key
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial|Slide", meta = (DisplayName = "On Slide Setup"))
	void BP_OnSlideSetup(UTexture2D* InSlideImage, const FText& InCloseHintText, UTexture2D* InCloseKeyIcon);

	/**
	 * Called when slide should be hidden
	 * Implement in Blueprint to play hide animation, then call OnHideAnimationFinished
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial|Slide", meta = (DisplayName = "On Hide Slide"))
	void BP_OnHideSlide();

	/**
	 * Call this from Blueprint when hide animation finishes
	 * Will remove the widget from parent
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Slide")
	void OnHideAnimationFinished();

	// ==================== Accessors ====================

	/** Get the slide image */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Slide")
	UTexture2D* GetSlideImage() const { return SlideImage; }

	/** Get the close action */
	UFUNCTION(BlueprintPure, Category = "Tutorial|Slide")
	UInputAction* GetCloseAction() const { return CloseAction; }

protected:

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** Current slide image */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Slide")
	TObjectPtr<UTexture2D> SlideImage;

	/** Close hint text */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Slide")
	FText CloseHintText;

	/** Close key icon */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Slide")
	TObjectPtr<UTexture2D> CloseKeyIcon;

	/** Input action that closes the slide */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Slide")
	TObjectPtr<UInputAction> CloseAction;

	/** Expected key for closing (resolved from CloseAction) */
	FKey ExpectedCloseKey;

	/** Is widget being hidden */
	bool bIsHiding = false;

	/** Get TutorialSubsystem reference */
	UTutorialSubsystem* GetTutorialSubsystem() const;
};