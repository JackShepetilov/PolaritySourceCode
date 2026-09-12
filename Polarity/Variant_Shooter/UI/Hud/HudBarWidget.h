// HudBarWidget.h
// A leaning bar: the fill snaps to the truth, a pale ghost drains after it so a hit stays readable.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "HudShapeWidget.h"
#include "HudBarWidget.generated.h"

/**
 * Inherit in Blueprint (WBP_HudBar): same Shape image as the plate, with a track colour. On a drop
 * the fill goes straight to the new value and the ghost waits LagDelay, then drains to meet it, so
 * the eye sees how much was lost. On a rise the fill sweeps up over RiseTime and the ghost rides on it.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UHudBarWidget : public UHudShapeWidget
{
	GENERATED_BODY()

public:

	/** Palette colour of the ghost that trails a drop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Bar", meta = (Categories = "Palette"))
	FGameplayTag LagColorTag;

	/** Seconds the ghost holds before draining. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Bar", meta = (ClampMin = "0"))
	float LagDelay = 0.35f;

	/** Seconds the ghost takes to drain the full bar; a small hit drains proportionally faster. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Bar", meta = (ClampMin = "0.05"))
	float LagDrainTime = 0.6f;

	/** Seconds the fill takes to sweep up on a heal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Bar", meta = (ClampMin = "0"))
	float RiseTime = 0.25f;

	/** Dark lines dividing the bar into this many parts. 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Bar", meta = (ClampMin = "0"))
	int32 Segments = 4;

	/** 0..1. bInstant skips every motion (first draw, respawn). */
	UFUNCTION(BlueprintCallable, Category = "HUD Bar")
	void SetFraction(float Fraction, bool bInstant = false);

	UFUNCTION(BlueprintPure, Category = "HUD Bar")
	float GetFraction() const { return Target; }

protected:

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void PushShapeParameters() override;

private:

	float Target = 1.0f;
	float Fill = 1.0f;
	float Lag = 1.0f;
	float LagWait = 0.0f;
	float RiseFrom = 1.0f;
	float RiseElapsed = 1.0f;

	void PushFill();
};
