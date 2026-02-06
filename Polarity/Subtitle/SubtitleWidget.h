// SubtitleWidget.h
// Base widget for displaying subtitles

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SubtitleWidget.generated.h"

/**
 * Base widget class for displaying subtitles.
 * Derive from this class in Blueprint to implement the visual design.
 *
 * Typical Blueprint implementation:
 * 1. Create a UMG widget inheriting from this class
 * 2. Add a TextBlock for subtitle text
 * 3. Optionally add a TextBlock for speaker name
 * 4. Implement BP_OnShowSubtitle to set text and play show animation
 * 5. Implement BP_OnHideSubtitle to play hide animation, then call OnHideAnimationFinished
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API USubtitleWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	// ==================== API (called by SubtitleSubsystem) ====================

	/**
	 * Show a subtitle with the given text and duration.
	 * @param InText - The subtitle text to display
	 * @param InSpeaker - Optional speaker name (e.g., "BOSS", "NARRATOR")
	 * @param InDuration - How long the subtitle will be shown (for animations)
	 */
	UFUNCTION(BlueprintCallable, Category = "Subtitle")
	void ShowSubtitle(const FText& InText, const FText& InSpeaker, float InDuration);

	/**
	 * Hide the subtitle.
	 * Called when duration expires or subtitle is skipped.
	 */
	UFUNCTION(BlueprintCallable, Category = "Subtitle")
	void HideSubtitle();

	// ==================== Blueprint Events ====================

	/**
	 * Called when a subtitle should be displayed.
	 * Implement in Blueprint to set text content and play show animation.
	 * @param InText - The subtitle text
	 * @param InSpeaker - Speaker name (may be empty)
	 * @param InDuration - Total display duration (for typewriter effects, etc.)
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Subtitle", meta = (DisplayName = "On Show Subtitle"))
	void BP_OnShowSubtitle(const FText& InText, const FText& InSpeaker, float InDuration);

	/**
	 * Called when the subtitle should be hidden.
	 * Implement in Blueprint to play hide animation, then call OnHideAnimationFinished.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Subtitle", meta = (DisplayName = "On Hide Subtitle"))
	void BP_OnHideSubtitle();

	/**
	 * Call this from Blueprint when hide animation finishes.
	 * Signals the subsystem that the widget is ready for the next subtitle.
	 */
	UFUNCTION(BlueprintCallable, Category = "Subtitle")
	void OnHideAnimationFinished();

	// ==================== Accessors ====================

	/** Get the current subtitle text */
	UFUNCTION(BlueprintPure, Category = "Subtitle")
	FText GetSubtitleText() const { return SubtitleText; }

	/** Get the current speaker name */
	UFUNCTION(BlueprintPure, Category = "Subtitle")
	FText GetSpeaker() const { return Speaker; }

	/** Get the subtitle duration */
	UFUNCTION(BlueprintPure, Category = "Subtitle")
	float GetDuration() const { return Duration; }

	/** Check if subtitle is currently visible */
	UFUNCTION(BlueprintPure, Category = "Subtitle")
	bool IsSubtitleVisible() const { return bIsVisible; }

	/** Check if there is a speaker name */
	UFUNCTION(BlueprintPure, Category = "Subtitle")
	bool HasSpeaker() const { return !Speaker.IsEmpty(); }

protected:

	/** Current subtitle text */
	UPROPERTY(BlueprintReadOnly, Category = "Subtitle")
	FText SubtitleText;

	/** Current speaker name */
	UPROPERTY(BlueprintReadOnly, Category = "Subtitle")
	FText Speaker;

	/** Current duration */
	UPROPERTY(BlueprintReadOnly, Category = "Subtitle")
	float Duration = 0.0f;

	/** Is the subtitle currently visible */
	UPROPERTY(BlueprintReadOnly, Category = "Subtitle")
	bool bIsVisible = false;

	/** Is the widget currently hiding (playing hide animation) */
	bool bIsHiding = false;
};
