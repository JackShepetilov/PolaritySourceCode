// UpgradeCardWidget.h
// One card in the level-up choice panel. Inherit in Blueprint (WBP_UpgradeCard)
// to design layout (icon, name, description, tier, "Choose" button).
// In BP, hook the button's OnClicked to RequestSelect, and bind OnSelected
// in the parent ChoiceWidget.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UpgradeCardWidget.generated.h"

class UUpgradeDefinition;
class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUpgradeCardSelected, int32, Index);

UCLASS(Abstract, Blueprintable)
class POLARITY_API UUpgradeCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Fill the card with upgrade data. Index identifies the slot in the parent ChoiceWidget. */
	UFUNCTION(BlueprintCallable, Category = "Upgrade Card")
	void InitFromDefinition(UUpgradeDefinition* InDefinition, int32 InIndex);

	/** Hook to your "Choose" button in BP. Broadcasts OnSelected with this card's Index. */
	UFUNCTION(BlueprintCallable, Category = "Upgrade Card")
	void RequestSelect();

	/** Parent ChoiceWidget binds to this. */
	UPROPERTY(BlueprintAssignable, Category = "Upgrade Card")
	FOnUpgradeCardSelected OnSelected;

	// ==================== Data (set by InitFromDefinition) ====================

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Card")
	TObjectPtr<UUpgradeDefinition> Definition;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Card")
	int32 Index = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Card")
	FText UpgradeName;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Card")
	FText UpgradeDescription;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Card")
	TObjectPtr<UTexture2D> UpgradeIcon;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Card")
	int32 UpgradeTier = 1;

protected:
	/**
	 * Implement in BP to populate widgets and play intro animation.
	 * Data is passed via parameters (not relying on inherited variables).
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Upgrade Card",
		meta = (DisplayName = "On Initialized"))
	void BP_OnInitialized(const FText& InName, const FText& InDescription, UTexture2D* InIcon, int32 InTier);
};
