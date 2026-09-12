// HudVitalsWidget.h
// HUD.Slot.Vitals: the player's own health as one leaning bar with the number inside.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "HudJuice.h"
#include "HudSlotWidget.h"
#include "HudVitalsWidget.generated.h"

class UHudBarWidget;
class UHudShapeWidget;
class UTextBlock;

/**
 * Inherit in Blueprint (WBP_Vitals): a plate (optional), a UHudBarWidget named HealthBar and a
 * text block named HealthText. Everything else is here: the bar drops and ghosts, the number rolls
 * and punches, the plate flashes on a hit and on a heal and breathes when health is low.
 * Armour and shield come later into this same widget.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UHudVitalsWidget : public UHudSlotWidget
{
	GENERATED_BODY()

public:

	/** Palette colour of the small caption above the bar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals", meta = (Categories = "Palette"))
	FGameplayTag CaptionColorTag;

	/** Palette colour the plate flashes on a hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals|Juice", meta = (Categories = "Palette"))
	FGameplayTag HitFlashTag;

	/** Palette colour the plate flashes on a heal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals|Juice", meta = (Categories = "Palette"))
	FGameplayTag HealFlashTag;

	/** Below this fraction the plate pulses. 0 turns it off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals|Juice", meta = (ClampMin = "0", ClampMax = "1"))
	float LowHealthFraction = 0.3f;

	/** Palette colour the plate pulses toward while health is low. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals|Juice", meta = (Categories = "Palette"))
	FGameplayTag LowHealthPulseTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals|Juice", meta = (ClampMin = "0.1"))
	float LowHealthPulseHz = 1.4f;

protected:

	virtual void NativeConstruct() override;
	virtual void NativeBind(AShooterCharacter* Character) override;
	virtual void NativeUnbind() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void HandleHealthChanged(float CurrentHP, float MaxHP, float LifePercent, float ArmorPercent);

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", meta = (BindWidget))
	TObjectPtr<UHudBarWidget> HealthBar;

	/** Optional: the number. The bar alone is the agreed look, so most layouts leave this out. */
	UPROPERTY(BlueprintReadOnly, Category = "Vitals", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HealthText;

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", meta = (BindWidgetOptional))
	TObjectPtr<UHudShapeWidget> Plate;

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Caption;

private:

	void Apply(float CurrentHP, float MaxHP, bool bInstant);

	FHudNumberTween Number;
	float LastFraction = 1.0f;
	float PulseTime = 0.0f;
	bool bPulsing = false;
	FLinearColor PlateRest = FLinearColor::Black;
};
