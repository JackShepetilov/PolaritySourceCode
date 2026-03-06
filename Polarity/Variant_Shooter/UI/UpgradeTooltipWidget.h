// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UpgradeTooltipWidget.generated.h"

class UUpgradeDefinition;

/**
 * Base class for the upgrade pickup tooltip.
 * Inherit in Blueprint to create the visual layout (icon, name, description, tier).
 * Attached to AUpgradePickup as a world-space UWidgetComponent.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UUpgradeTooltipWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/**
	 * Initialize the tooltip from an upgrade definition.
	 * Sets all properties and calls the Blueprint event.
	 */
	UFUNCTION(BlueprintCallable, Category = "Upgrade Tooltip")
	void InitFromDefinition(UUpgradeDefinition* Definition);

	// ==================== Blueprint Events ====================

	/** Called after properties are set — build your UI here */
	UFUNCTION(BlueprintImplementableEvent, Category = "Upgrade Tooltip",
		meta = (DisplayName = "On Tooltip Initialized"))
	void BP_OnTooltipInitialized();

	/** Called when player enters tooltip range — play show animation */
	UFUNCTION(BlueprintImplementableEvent, Category = "Upgrade Tooltip",
		meta = (DisplayName = "On Tooltip Show"))
	void BP_OnTooltipShow();

	/** Called when player leaves tooltip range — play hide animation */
	UFUNCTION(BlueprintImplementableEvent, Category = "Upgrade Tooltip",
		meta = (DisplayName = "On Tooltip Hide"))
	void BP_OnTooltipHide();

	// ==================== Data (set by InitFromDefinition) ====================

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Tooltip")
	FText UpgradeName;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Tooltip")
	FText UpgradeDescription;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Tooltip")
	TObjectPtr<UTexture2D> UpgradeIcon;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade Tooltip")
	int32 UpgradeTier = 1;
};
