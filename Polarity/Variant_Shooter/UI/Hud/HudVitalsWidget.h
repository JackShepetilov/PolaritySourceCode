// HudVitalsWidget.h
// HUD.Slot.Vitals: the player's own health as one leaning bar with the number inside.

#pragma once

#include "CoreMinimal.h"
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals|Juice")
	FLinearColor HitFlash = FLinearColor(1.0f, 0.95f, 0.9f, 0.95f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals|Juice")
	FLinearColor HealFlash = FLinearColor(0.35f, 1.0f, 0.55f, 0.9f);

	/** Below this fraction the plate pulses. 0 turns it off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals|Juice", meta = (ClampMin = "0", ClampMax = "1"))
	float LowHealthFraction = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals|Juice")
	FLinearColor LowHealthPulse = FLinearColor(0.45f, 0.05f, 0.05f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals|Juice", meta = (ClampMin = "0.1"))
	float LowHealthPulseHz = 1.4f;

protected:

	virtual void NativeBind(AShooterCharacter* Character) override;
	virtual void NativeUnbind() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void HandleHealthChanged(float CurrentHP, float MaxHP, float LifePercent, float ArmorPercent);

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", meta = (BindWidget))
	TObjectPtr<UHudBarWidget> HealthBar;

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", meta = (BindWidget))
	TObjectPtr<UTextBlock> HealthText;

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", meta = (BindWidgetOptional))
	TObjectPtr<UHudShapeWidget> Plate;

private:

	void Apply(float CurrentHP, float MaxHP, bool bInstant);

	FHudNumberTween Number;
	float LastFraction = 1.0f;
	float PulseTime = 0.0f;
	bool bPulsing = false;
	FLinearColor PlateRest = FLinearColor::Black;
};
