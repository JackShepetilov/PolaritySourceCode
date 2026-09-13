// BuildMenuWidget.h
// HUD.Slot.BuildMenu: the engineer's PDA. One row per kind of building: key, icon, name, price,
// and whether the player can have it right now.
//
// Follows UBuilderComponent (open, closed, placing) and AShooterPlayerState (metal, what stands),
// and redraws from those. It never tells the builder anything: the keys do, through the builder's
// own input. Collapsed until the menu opens.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Variant_Shooter/UI/Hud/HudSlotWidget.h"
#include "BuilderComponent.h"
#include "BuildMenuWidget.generated.h"

class AShooterPlayerState;
class UBuildableDefinition;
class UHudShapeWidget;
class UImage;
class UPanelWidget;
class USoundBase;
class UTextBlock;

UENUM(BlueprintType)
enum class EBuildMenuEntryState : uint8
{
	/** Can be built right now. */
	Available,
	/** Not enough metal. */
	Unaffordable,
	/** Every allowed copy is standing; the key now demolishes. */
	Built
};

/**
 * One row of the menu. Inherit in Blueprint (WBP_BuildMenuEntry) with any of the optional widgets
 * named below; C++ fills whichever exist. Colours are palette tags handed down by the menu.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UBuildMenuEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	void Setup(const UBuildableDefinition* Definition, int32 KeyNumber);
	void SetState(EBuildMenuEntryState State, int32 BuiltCount, int32 MaxCount, bool bSelected);

	/** The key was pressed and nothing could be done: too poor, or the kind is full. The plate
	 *  flashes so the press is seen to land somewhere, which a log line never is. */
	void FlashRefused(FLinearColor Color);

	UPROPERTY(BlueprintReadOnly, Category = "Build Menu")
	FLinearColor AvailableColor = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Build Menu")
	FLinearColor UnaffordableColor = FLinearColor::Gray;

	UPROPERTY(BlueprintReadOnly, Category = "Build Menu")
	FLinearColor BuiltColor = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Build Menu")
	FLinearColor PlateRestColor = FLinearColor::Black;

	UPROPERTY(BlueprintReadOnly, Category = "Build Menu")
	FLinearColor PlateSelectedColor = FLinearColor::Black;

protected:

	UFUNCTION(BlueprintImplementableEvent, Category = "Build Menu", meta = (DisplayName = "On State Changed"))
	void BP_OnStateChanged(EBuildMenuEntryState State, int32 BuiltCount, int32 MaxCount, bool bSelected);

	UFUNCTION(BlueprintImplementableEvent, Category = "Build Menu", meta = (DisplayName = "On Refused"))
	void BP_OnRefused();

	UPROPERTY(BlueprintReadOnly, Category = "Build Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyText;

	UPROPERTY(BlueprintReadOnly, Category = "Build Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(BlueprintReadOnly, Category = "Build Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CostText;

	/** "built", "2 / 2", or empty. */
	UPROPERTY(BlueprintReadOnly, Category = "Build Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(BlueprintReadOnly, Category = "Build Menu", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Icon;

	UPROPERTY(BlueprintReadOnly, Category = "Build Menu", meta = (BindWidgetOptional))
	TObjectPtr<UHudShapeWidget> Plate;
};

/**
 * Inherit in Blueprint (WBP_BuildMenu): a panel named EntryPanel (a VerticalBox or a
 * HorizontalBox) that the rows are created into, and EntryClass set to the row Blueprint.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UBuildMenuWidget : public UHudSlotWidget
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Build Menu")
	TSubclassOf<UBuildMenuEntryWidget> EntryClass;

	/** Number shown on the first row. The slot keys are the builder's business; this is only what
	 *  is printed beside each row. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Build Menu", meta = (ClampMin = "0"))
	int32 FirstKeyNumber = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Build Menu|Colors", meta = (Categories = "Palette"))
	FGameplayTag AvailableColorTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Build Menu|Colors", meta = (Categories = "Palette"))
	FGameplayTag UnaffordableColorTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Build Menu|Colors", meta = (Categories = "Palette"))
	FGameplayTag BuiltColorTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Build Menu|Colors", meta = (Categories = "Palette"))
	FGameplayTag PlateColorTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Build Menu|Colors", meta = (Categories = "Palette"))
	FGameplayTag PlateSelectedColorTag;

	/** The plate flashes this when a key is pressed on a row that cannot be built (no metal, or the
	 *  kind is full). The metal plate's spend flash is the natural choice. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Build Menu|Colors", meta = (Categories = "Palette"))
	FGameplayTag RefusedFlashTag;

	/** Played 2D, on this screen only, together with the flash: the "you cannot do that" buzz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Build Menu")
	TObjectPtr<USoundBase> RefusedSound;

	/** Stay on screen while the ghost is out (with the chosen row lit), or hide with the menu. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Build Menu")
	bool bShowWhilePlacing = true;

protected:

	virtual void NativeConstruct() override;
	virtual void NativeBind(AShooterCharacter* Character) override;
	virtual void NativeUnbind() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void HandleModeChanged(EBuilderMode Mode);

	UFUNCTION()
	void HandleMetalChanged(int32 Metal, int32 MaxMetal, int32 Delta);

	UFUNCTION()
	void HandleOwnedBuildablesChanged();

	UFUNCTION()
	void HandleSlotRefused(int32 SlotIndex, EBuildablePlacementResult Result);

	UFUNCTION(BlueprintImplementableEvent, Category = "Build Menu", meta = (DisplayName = "On Menu Shown"))
	void BP_OnMenuShown(bool bShown);

	UPROPERTY(BlueprintReadOnly, Category = "Build Menu", meta = (BindWidget))
	TObjectPtr<UPanelWidget> EntryPanel;

private:

	void TryBindState();
	void RebuildEntries();
	void RefreshEntries();
	void ApplyMode(EBuilderMode Mode);

	TWeakObjectPtr<UBuilderComponent> Builder;
	TWeakObjectPtr<AShooterPlayerState> State;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBuildMenuEntryWidget>> Entries;

	bool bMenuVisible = false;
};
