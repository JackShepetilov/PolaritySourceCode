// HudMetalWidget.h
// HUD.Slot.Metal: the player's metal, a number that punches when it moves.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "HudJuice.h"
#include "HudSlotWidget.h"
#include "HudMetalWidget.generated.h"

class AShooterPlayerState;
class UHudShapeWidget;
class UImage;
class UPanelWidget;
class UTextBlock;

/**
 * Inherit in Blueprint (WBP_Metal): a plate (optional), a text block MetalText, an optional
 * MetalMaxText, an optional MetalIcon beside the number, and an optional DeltaStack panel above
 * the plate where every change floats up as "+25" / "-19" (see FHudDeltaStack for the rules).
 * Reads AShooterPlayerState::OnMetalChanged. The PlayerState can arrive after the character on a
 * client, so the bind keeps looking for it each tick until it is there.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UHudMetalWidget : public UHudSlotWidget
{
	GENERATED_BODY()

public:

	/** Palette colour of the small caption above the number. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metal", meta = (Categories = "Palette"))
	FGameplayTag CaptionColorTag;

	/** Palette colour of the number itself. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metal", meta = (Categories = "Palette"))
	FGameplayTag NumberColorTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metal|Juice", meta = (Categories = "Palette"))
	FGameplayTag GainFlashTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metal|Juice", meta = (Categories = "Palette"))
	FGameplayTag SpendFlashTag;

	/** Plate tint while metal is at the cap, so the player knows picking up more is pointless. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metal|Juice", meta = (Categories = "Palette"))
	FGameplayTag FullTintTag;

	// ==================== Deltas: the "+25" / "-19" that float over the plate ====================

	/** Colour of a gain number. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metal|Deltas", meta = (Categories = "Palette"))
	FGameplayTag GainColorTag;

	/** Colour of a loss number. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metal|Deltas", meta = (Categories = "Palette"))
	FGameplayTag LossColorTag;

	/** Font of the floating numbers. Unset = the number's own font at DeltaFontSize. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metal|Deltas")
	FSlateFontInfo DeltaFont;

	/** Point size used when DeltaFont is left unset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metal|Deltas", meta = (ClampMin = "6"))
	int32 DeltaFontSize = 18;

	/** Seconds a number stays before fading. A same-sign change in that time is added into it and
	 *  the time restarts, so a scrap pile reads as one growing number. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metal|Deltas", meta = (ClampMin = "0"))
	float DeltaHoldTime = 0.9f;

	/** Seconds the rise-and-fade takes after the hold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metal|Deltas", meta = (ClampMin = "0.05"))
	float DeltaFadeTime = 0.4f;

	/** Slate units a number drifts up while fading. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metal|Deltas", meta = (ClampMin = "0"))
	float DeltaRise = 14.0f;

	/** Most numbers up at once. Gains and losses never merge, so two is the working minimum. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metal|Deltas", meta = (ClampMin = "1"))
	int32 MaxDeltas = 3;

protected:

	virtual void NativeConstruct() override;
	virtual void NativeBind(AShooterCharacter* Character) override;
	virtual void NativeUnbind() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void HandleMetalChanged(int32 Metal, int32 MaxMetal, int32 Delta);

	UPROPERTY(BlueprintReadOnly, Category = "Metal", meta = (BindWidget))
	TObjectPtr<UTextBlock> MetalText;

	UPROPERTY(BlueprintReadOnly, Category = "Metal", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MetalMaxText;

	UPROPERTY(BlueprintReadOnly, Category = "Metal", meta = (BindWidgetOptional))
	TObjectPtr<UHudShapeWidget> Plate;

	UPROPERTY(BlueprintReadOnly, Category = "Metal", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Caption;

	/** The picture beside the number. Left untinted: it is art, the palette does not own it. */
	UPROPERTY(BlueprintReadOnly, Category = "Metal", meta = (BindWidgetOptional))
	TObjectPtr<UImage> MetalIcon;

	/** Where the floating numbers go: a VerticalBox aligned to its bottom edge, sitting above the
	 *  plate, so a new number appears nearest the plate and pushes the older ones up. */
	UPROPERTY(BlueprintReadOnly, Category = "Metal", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> DeltaStack;

private:

	void TryBindState();
	void Apply(int32 Metal, int32 MaxMetal, int32 Delta, bool bInstant);

	TWeakObjectPtr<AShooterPlayerState> State;
	FHudNumberTween Number;
	FHudDeltaStack Deltas;
	FLinearColor PlateRest = FLinearColor::Black;
	bool bAtCap = false;
};
