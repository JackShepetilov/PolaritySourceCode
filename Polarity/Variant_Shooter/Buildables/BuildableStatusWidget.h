// BuildableStatusWidget.h
// HUD.Slot.Buildables: the state of everything this player has standing, one row per building,
// the way TF2 keeps the engineer's buildings in the corner. Health, level, and whether it is still
// going up. Empty (collapsed) when there is nothing to show.
//
// Reads AShooterPlayerState::OwnedBuildables for the rows and each ABuildableActor's
// OnBuildableChanged for the numbers. Nothing pushes at it.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Variant_Shooter/UI/Hud/HudSlotWidget.h"
#include "BuildableStatusWidget.generated.h"

class ABuildableActor;
class AShooterPlayerState;
class UHudBarWidget;
class UImage;
class UPanelWidget;
class UTextBlock;

/**
 * One row. Inherit in Blueprint (WBP_BuildableStatusEntry) with any of the optional widgets below.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UBuildableStatusEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	void Follow(ABuildableActor* Buildable);
	void Unfollow();

	UFUNCTION(BlueprintPure, Category = "Buildable Status")
	ABuildableActor* GetBuildable() const { return Buildable.Get(); }

	UPROPERTY(BlueprintReadOnly, Category = "Buildable Status")
	FLinearColor TextColor = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Buildable Status")
	FLinearColor DimTextColor = FLinearColor::Gray;

protected:

	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleChanged(ABuildableActor* Changed);

	void Refresh(bool bInstant);

	UFUNCTION(BlueprintImplementableEvent, Category = "Buildable Status", meta = (DisplayName = "On Refreshed"))
	void BP_OnRefreshed(ABuildableActor* Followed);

	UPROPERTY(BlueprintReadOnly, Category = "Buildable Status", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Icon;

	UPROPERTY(BlueprintReadOnly, Category = "Buildable Status", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NameText;

	/** "1", "2", "3". */
	UPROPERTY(BlueprintReadOnly, Category = "Buildable Status", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LevelText;

	/** "building 40%", "upgrade 75 / 200", or empty when simply standing. */
	UPROPERTY(BlueprintReadOnly, Category = "Buildable Status", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(BlueprintReadOnly, Category = "Buildable Status", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HealthText;

	UPROPERTY(BlueprintReadOnly, Category = "Buildable Status", meta = (BindWidgetOptional))
	TObjectPtr<UHudBarWidget> HealthBar;

private:

	TWeakObjectPtr<ABuildableActor> Buildable;
};

/**
 * Inherit in Blueprint (WBP_BuildableStatus): a panel named EntryPanel that rows are created into,
 * and EntryClass set to the row Blueprint.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UBuildableStatusWidget : public UHudSlotWidget
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buildable Status")
	TSubclassOf<UBuildableStatusEntryWidget> EntryClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buildable Status|Colors", meta = (Categories = "Palette"))
	FGameplayTag TextColorTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buildable Status|Colors", meta = (Categories = "Palette"))
	FGameplayTag DimTextColorTag;

protected:

	virtual void NativeBind(AShooterCharacter* Character) override;
	virtual void NativeUnbind() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void HandleOwnedBuildablesChanged();

	UPROPERTY(BlueprintReadOnly, Category = "Buildable Status", meta = (BindWidget))
	TObjectPtr<UPanelWidget> EntryPanel;

private:

	void TryBindState();
	void RebuildEntries();

	TWeakObjectPtr<AShooterPlayerState> State;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBuildableStatusEntryWidget>> Entries;
};
