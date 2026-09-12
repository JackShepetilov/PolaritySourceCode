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
class UTextBlock;

/**
 * Inherit in Blueprint (WBP_Metal): a plate (optional), a text block MetalText, an optional
 * MetalMaxText. Reads AShooterPlayerState::OnMetalChanged. The PlayerState can arrive after the
 * character on a client, so the bind keeps looking for it each tick until it is there.
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

private:

	void TryBindState();
	void Apply(int32 Metal, int32 MaxMetal, int32 Delta, bool bInstant);

	TWeakObjectPtr<AShooterPlayerState> State;
	FHudNumberTween Number;
	FLinearColor PlateRest = FLinearColor::Black;
	bool bAtCap = false;
};
