// ReminderPanelWidget.h
// Witcher-style reminder panel showing mini-hints when dismiss key is held

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TutorialTypes.h"
#include "TutorialHintWidget.h"
#include "ReminderPanelWidget.generated.h"

/**
 * Display data for the reminder panel.
 * Each entry is resolved through the InputAction pipeline (same as regular hints).
 */
USTRUCT(BlueprintType)
struct FReminderDisplayData
{
	GENERATED_BODY()

	/** Resolved single-hint display data for each entry */
	UPROPERTY(BlueprintReadOnly, Category = "Reminder")
	TArray<FHintDisplayData> Entries;

	/** Resolved multi-hint display data for each entry */
	UPROPERTY(BlueprintReadOnly, Category = "Reminder")
	TArray<FMultiHintDisplayData> MultiEntries;

	/** Flag per entry: non-zero if this entry uses multi-hint layout */
	UPROPERTY(BlueprintReadOnly, Category = "Reminder")
	TArray<uint8> IsMultiHint;

	/** Resolved icon for the dismiss key (shown as "hold [key] for hints") */
	UPROPERTY(BlueprintReadOnly, Category = "Reminder")
	FTutorialInputIconData DismissIcon;
};

/**
 * Base widget class for the reminder panel (Witcher-style hint overlay).
 * Shows a compact list of mini-hints in the corner of the screen when the dismiss key is held.
 * Derive from this class in Blueprint to implement the visual design.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UReminderPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	// ==================== Blueprint Events ====================

	/**
	 * Called when reminder data is set up (resolved icons + text).
	 * Blueprint should create mini-hint entries in a VerticalBox.
	 * @param DisplayData - All resolved display data for the panel
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial|Reminder",
			  meta = (DisplayName = "On Reminder Setup"))
	void BP_OnReminderSetup(const FReminderDisplayData& DisplayData);

	/**
	 * Called when the panel should become visible (dismiss key pressed).
	 * Blueprint should play a show animation (e.g., fade in).
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial|Reminder",
			  meta = (DisplayName = "On Show Reminder"))
	void BP_OnShowReminder();

	/**
	 * Called when the panel should hide (dismiss key released).
	 * Blueprint should play a hide animation, then SetVisibility(Collapsed).
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial|Reminder",
			  meta = (DisplayName = "On Hide Reminder"))
	void BP_OnHideReminder();
};
