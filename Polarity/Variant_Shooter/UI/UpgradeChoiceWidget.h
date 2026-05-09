// UpgradeChoiceWidget.h
// Modal panel that opens on per-skill level-up, presents N random upgrades from THAT skill's pool,
// applies the chosen one, then processes any queued level-ups.
//
// Lifecycle:
//   NativeConstruct subscribes to UXPSubsystem::OnSkillLevelUp.
//   On level-up: rolls choices filtered by Category, pauses game, fires BP_OnChoiceOpened(Category).
//   BP spawns N UUpgradeCardWidget instances from CurrentChoices and binds OnSelected -> ConfirmChoice.
//   ConfirmChoice grants the upgrade, closes panel, and processes the next queued category if any.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkillTypes.h"
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
	ESkillCategory GetCurrentCategory() const { return CurrentCategory; }

	UFUNCTION(BlueprintPure, Category = "Upgrade Choice")
	bool IsChoiceOpen() const { return bIsOpen; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ==================== Blueprint Events ====================

	/**
	 * BP must implement: read GetCurrentChoices(), spawn N card widgets,
	 * call InitFromDefinition on each, bind their OnSelected -> ConfirmChoice(Index).
	 * Use Category to colour/decorate cards (Movement / Melee / EMF / Weapon).
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Upgrade Choice",
		meta = (DisplayName = "On Choice Opened"))
	void BP_OnChoiceOpened(ESkillCategory Category);

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
	void HandleSkillLevelUp(ESkillCategory Category, int32 NewLevel);

	void OpenChoice(ESkillCategory Category);
	void CloseChoice(UUpgradeDefinition* SelectedDefinition);
	void RollChoicesForCategory(ESkillCategory Category);
	void TryProcessNextPending();

	UXPSubsystem* GetXPSubsystem() const;
	UUpgradeManagerComponent* GetUpgradeManager() const;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Choice")
	TArray<TObjectPtr<UUpgradeDefinition>> CurrentChoices;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Choice")
	ESkillCategory CurrentCategory = ESkillCategory::Movement;

	UPROPERTY(Transient)
	bool bIsOpen = false;

	/** FIFO queue of skill categories that levelled up while choice was open. */
	TArray<ESkillCategory> PendingCategories;
};
