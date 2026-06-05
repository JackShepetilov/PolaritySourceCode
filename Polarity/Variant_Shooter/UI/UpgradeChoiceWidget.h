// UpgradeChoiceWidget.h
// Modal panel that opens on level-up, presents N random upgrades from the WHOLE registry,
// applies the chosen one, then processes any queued level-ups.
//
// Lifecycle:
//   NativeConstruct subscribes to the deferred level-up release (fallback: UXPSubsystem::OnLevelUp).
//   On level-up: rolls choices from the full registry (minus maxed-out), pauses game, fires BP_OnChoiceOpened().
//   BP spawns N UUpgradeCardWidget instances from CurrentChoices and binds OnSelected -> ConfirmChoice.
//   ConfirmChoice grants the upgrade, closes panel, and processes the next queued level-up if any.
//
// Card theming: each card reads its own UpgradeCard.Definition.Category (cosmetic) for colour/icon —
// a single roll can now mix categories, so there is no panel-wide category.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UpgradeChoiceWidget.generated.h"

class UXPSubsystem;
class UUpgradeRegistry;
class UUpgradeDefinition;
class UUpgradeManagerComponent;

UCLASS(Abstract, Blueprintable)
class POLARITY_API UUpgradeChoiceWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** BP calls this when the player clicks a card. Applies the upgrade and closes the panel. */
	UFUNCTION(BlueprintCallable, Category = "Upgrade Choice")
	void ConfirmChoice(int32 Index);

	UFUNCTION(BlueprintPure, Category = "Upgrade Choice")
	const TArray<UUpgradeDefinition*>& GetCurrentChoices() const { return CurrentChoices; }

	UFUNCTION(BlueprintPure, Category = "Upgrade Choice")
	bool IsChoiceOpen() const { return bIsOpen; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ==================== Blueprint Events ====================

	/**
	 * BP must implement: read GetCurrentChoices(), spawn N card widgets,
	 * call InitFromDefinition on each, bind their OnSelected -> ConfirmChoice(Index).
	 * Colour/decorate each card from its own UpgradeCard.Definition.Category.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Upgrade Choice",
		meta = (DisplayName = "On Choice Opened"))
	void BP_OnChoiceOpened();

	UFUNCTION(BlueprintImplementableEvent, Category = "Upgrade Choice",
		meta = (DisplayName = "On Choice Closed"))
	void BP_OnChoiceClosed(UUpgradeDefinition* SelectedDefinition);

	// ==================== Configuration ====================

	UPROPERTY(EditDefaultsOnly, Category = "Upgrade Choice", meta = (ClampMin = "1", ClampMax = "8"))
	int32 ChoiceCount = 3;

	/** Registry of all available upgrades. Set in WBP class defaults. */
	UPROPERTY(EditDefaultsOnly, Category = "Upgrade Choice")
	TObjectPtr<UUpgradeRegistry> Registry;

	// ==================== Internal ====================

	UFUNCTION()
	void HandleLevelUp(int32 NewLevel);

	void OpenChoice();
	void CloseChoice(UUpgradeDefinition* SelectedDefinition);
	void RollChoices();
	void TryProcessNextPending();

	UXPSubsystem* GetXPSubsystem() const;
	UUpgradeManagerComponent* GetUpgradeManager() const;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Choice")
	TArray<TObjectPtr<UUpgradeDefinition>> CurrentChoices;

	UPROPERTY(Transient)
	bool bIsOpen = false;

	/** Count of level-ups that arrived while a choice was already open (processed FIFO after close). */
	int32 PendingLevelUps = 0;
};
