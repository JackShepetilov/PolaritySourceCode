// HudRootWidget.h
// The one widget on the player screen. Everything else is content inside one of its slots.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "HudRootWidget.generated.h"

class UHudSlot;

/**
 * Inherit in Blueprint (WBP_HudRoot): a Canvas Panel full of UHudSlot placeholders, each anchored
 * where the layout says. Nothing else belongs here; a widget that is not in a slot is not on the HUD.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UHudRootWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/** The placeholder for a tag, or null when the root has no such slot. */
	UFUNCTION(BlueprintPure, Category = "HUD Root")
	UHudSlot* FindSlot(FGameplayTag SlotTag) const;

	/** Every slot in this root, in tree order. */
	void GetSlots(TArray<UHudSlot*>& OutSlots) const;
};
