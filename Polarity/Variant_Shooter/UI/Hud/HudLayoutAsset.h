// HudLayoutAsset.h
// Which widget goes into which slot. The only thing the registry needs besides the root class.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"
#include "HudLayoutAsset.generated.h"

class UHudRootWidget;
class UUserWidget;

USTRUCT(BlueprintType)
struct FHudSlotBinding
{
	GENERATED_BODY()

	/** The slot in the root widget this content goes into. */
	UPROPERTY(EditAnywhere, Category = "Slot", meta = (Categories = "HUD.Slot"))
	FGameplayTag SlotTag;

	/** What to put there. Must implement IHudBindable (any UHudSlotWidget child, or one of the
	 *  existing bars). A class that does not is still placed, just never bound. */
	UPROPERTY(EditAnywhere, Category = "Slot")
	TSubclassOf<UUserWidget> WidgetClass;

	/** Created collapsed; something else shows it later (the XP bar waits for a run). */
	UPROPERTY(EditAnywhere, Category = "Slot")
	bool bStartHidden = false;
};

/**
 * One per game mode or experience. Swap the asset on the controller and the whole HUD changes;
 * swap one WidgetClass and one slot changes. Positions are not here: they live in RootClass.
 */
UCLASS(BlueprintType)
class POLARITY_API UHudLayoutAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	/** The canvas with the UHudSlot placeholders. */
	UPROPERTY(EditAnywhere, Category = "HUD")
	TSubclassOf<UHudRootWidget> RootClass;

	UPROPERTY(EditAnywhere, Category = "HUD", meta = (TitleProperty = "SlotTag"))
	TArray<FHudSlotBinding> Slots;
};
