// TurretFeedWidget.h
// HUD.Slot.TurretFeed: the list a player picks a gun from to give to a turret. One row per owned
// ranged gun: key, icon, name, rounds, and where in the turret it would go (or why it cannot).
//
// Follows UBuilderComponent (the Feeding mode, the refusals) and the turret it is open for (its
// vices fill up, its level rises), and redraws from those. It never tells the builder anything:
// the number keys do, through the builder's own input. Collapsed until the feed menu opens.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Variant_Shooter/UI/Hud/HudSlotWidget.h"
#include "BuilderComponent.h"
#include "TurretFeedWidget.generated.h"

class ABuildableActor;
class AShooterWeapon;
class ATurretBuildable;
class UHudShapeWidget;
class UImage;
class UPanelWidget;
class USoundBase;
class UTextBlock;

UENUM(BlueprintType)
enum class ETurretFeedEntryState : uint8
{
	/** The turret has a vice for this gun right now. */
	Available,
	/** No vice for it: the ordinary ones are full, or it is a heavy gun and the heavy vice is
	 *  locked or taken. */
	Unavailable
};

/**
 * One row of the feed menu. Inherit in Blueprint (WBP_TurretFeedEntry) with any of the optional
 * widgets named below; C++ fills whichever exist. Colours are palette tags handed down by the menu.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UTurretFeedEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	void Setup(const AShooterWeapon* Weapon, int32 KeyNumber);
	void SetState(ETurretFeedEntryState State, const FText& Status);

	/** The key was pressed and the turret had no vice for this gun. The plate flashes so the press
	 *  is seen to land somewhere. */
	void FlashRefused(FLinearColor Color);

	UPROPERTY(BlueprintReadOnly, Category = "Turret Feed")
	FLinearColor AvailableColor = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Turret Feed")
	FLinearColor UnavailableColor = FLinearColor::Gray;

	UPROPERTY(BlueprintReadOnly, Category = "Turret Feed")
	FLinearColor PlateRestColor = FLinearColor::Black;

protected:

	UFUNCTION(BlueprintImplementableEvent, Category = "Turret Feed", meta = (DisplayName = "On State Changed"))
	void BP_OnStateChanged(ETurretFeedEntryState State);

	UFUNCTION(BlueprintImplementableEvent, Category = "Turret Feed", meta = (DisplayName = "On Refused"))
	void BP_OnRefused();

	UPROPERTY(BlueprintReadOnly, Category = "Turret Feed", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyText;

	UPROPERTY(BlueprintReadOnly, Category = "Turret Feed", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NameText;

	/** "11 / 30": loaded rounds over the magazine. */
	UPROPERTY(BlueprintReadOnly, Category = "Turret Feed", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AmmoText;

	/** "vice 2", "needs level 3", "no free vice". */
	UPROPERTY(BlueprintReadOnly, Category = "Turret Feed", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(BlueprintReadOnly, Category = "Turret Feed", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Icon;

	UPROPERTY(BlueprintReadOnly, Category = "Turret Feed", meta = (BindWidgetOptional))
	TObjectPtr<UHudShapeWidget> Plate;
};

/**
 * Inherit in Blueprint (WBP_TurretFeed): a panel named EntryPanel (a VerticalBox) that the rows are
 * created into, an optional TitleText, and EntryClass set to the row Blueprint.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UTurretFeedWidget : public UHudSlotWidget
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret Feed")
	TSubclassOf<UTurretFeedEntryWidget> EntryClass;

	/** Number shown on the first row; the keys themselves are the builder's slot actions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret Feed", meta = (ClampMin = "0"))
	int32 FirstKeyNumber = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret Feed|Colors", meta = (Categories = "Palette"))
	FGameplayTag AvailableColorTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret Feed|Colors", meta = (Categories = "Palette"))
	FGameplayTag UnavailableColorTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret Feed|Colors", meta = (Categories = "Palette"))
	FGameplayTag PlateColorTag;

	/** The plate flashes this when a key lands on a gun the turret cannot take. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret Feed|Colors", meta = (Categories = "Palette"))
	FGameplayTag RefusedFlashTag;

	/** Played 2D, on this screen only, together with the flash. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret Feed")
	TObjectPtr<USoundBase> RefusedSound;

protected:

	virtual void NativeConstruct() override;
	virtual void NativeBind(AShooterCharacter* Character) override;
	virtual void NativeUnbind() override;

	UFUNCTION()
	void HandleModeChanged(EBuilderMode Mode);

	UFUNCTION()
	void HandleFeedRefused(int32 WeaponIndex);

	UFUNCTION()
	void HandleInventoryChanged();

	UFUNCTION()
	void HandleTurretChanged(ABuildableActor* Buildable);

	UFUNCTION(BlueprintImplementableEvent, Category = "Turret Feed", meta = (DisplayName = "On Menu Shown"))
	void BP_OnMenuShown(bool bShown);

	UPROPERTY(BlueprintReadOnly, Category = "Turret Feed", meta = (BindWidget))
	TObjectPtr<UPanelWidget> EntryPanel;

	/** "Turret, level 2: 1 of 2 vices free". */
	UPROPERTY(BlueprintReadOnly, Category = "Turret Feed", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

private:

	void RebuildEntries();
	void RefreshEntries();
	void ApplyMode(EBuilderMode Mode);
	void WatchTurret(ATurretBuildable* Turret);

	TWeakObjectPtr<UBuilderComponent> Builder;
	TWeakObjectPtr<ATurretBuildable> WatchedTurret;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTurretFeedEntryWidget>> Entries;

	bool bMenuVisible = false;
};
