// HudSlot.h
// A named place on the HUD root. Sits in the root widget's canvas; the registry fills it.

#pragma once

#include "CoreMinimal.h"
#include "Components/SizeBox.h"
#include "GameplayTagContainer.h"
#include "HudSlot.generated.h"

/**
 * Drop one into WBP_HudRoot's canvas per slot, anchor it, size it, and give it a HUD.Slot.* tag.
 * That is the whole layout: position lives here, in the designer, and nowhere in code. A SizeBox
 * so the designer sees the slot's footprint while it is still empty.
 */
UCLASS()
class POLARITY_API UHudSlot : public USizeBox
{
	GENERATED_BODY()

public:

	/** Which slot this is. Must be unique inside one root. */
	UPROPERTY(EditAnywhere, Category = "HUD Slot", meta = (Categories = "HUD.Slot"))
	FGameplayTag SlotTag;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif
};
