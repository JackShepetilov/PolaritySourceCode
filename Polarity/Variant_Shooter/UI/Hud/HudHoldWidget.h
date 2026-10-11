// HudHoldWidget.h
// HUD.Slot.Hold: "you are holding this key, and holding it is about to do something".

#pragma once

#include "CoreMinimal.h"
#include "HudSlotWidget.h"
#include "HudHoldWidget.generated.h"

class UHudBarWidget;
class UHudShapeWidget;
class UTextBlock;

/**
 * Inherit in Blueprint (WBP_Hold): a plate (optional), a text block KeyText for the key, a text
 * block LabelText for what the hold does, and a bar ProgressBar that fills as the hold runs.
 *
 * Shows whatever AShooterCharacter::GetHoldPrompt answers: the melee key winding up a hold attack,
 * the ability key on its way from a tap to an aimed throw. Its content is hidden while nothing is
 * being held.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UHudHoldWidget : public UHudSlotWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeBind(AShooterCharacter* Character) override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "Hold", meta = (BindWidgetOptional))
	TObjectPtr<UHudShapeWidget> Plate;

	UPROPERTY(BlueprintReadOnly, Category = "Hold", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyText;

	UPROPERTY(BlueprintReadOnly, Category = "Hold", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(BlueprintReadOnly, Category = "Hold", meta = (BindWidgetOptional))
	TObjectPtr<UHudBarWidget> ProgressBar;

private:
	bool bShown = false;
	FText ShownKey;
	FText ShownLabel;
};
