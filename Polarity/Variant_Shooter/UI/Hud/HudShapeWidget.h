// HudShapeWidget.h
// The HUD's one shape: a leaning plate drawn by M_HudShape at whatever size the widget has.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "HudJuice.h"
#include "HudShapeWidget.generated.h"

class UImage;
class UMaterialInstanceDynamic;

/**
 * Inherit in Blueprint (WBP_HudPlate) with an Image named Shape whose brush is M_HudShape. The
 * material draws a rounded parallelogram in pixel space, so the widget only has to tell it its
 * size (every tick, when it changes) and the colours. Plates put content in a Named Slot on top;
 * bars are the UHudBarWidget child.
 *
 * No colour is written in code: every colour is a Palette tag resolved through UPolarityPalette
 * at construct, so the look lives in Project Settings -> Polarity -> Palette and nowhere else.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UHudShapeWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/** How far the top edge leads the bottom one, in slate units, for a plate LeanReferenceHeight
	 *  tall. Shorter and taller plates lean by the same angle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Shape", meta = (ClampMin = "0"))
	float Lean = 30.0f;

	/** The height Lean is quoted for. 0 = Lean is absolute pixels at any height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Shape", meta = (ClampMin = "0"))
	float LeanReferenceHeight = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Shape", meta = (ClampMin = "0"))
	float Radius = 8.0f;

	/** Lean the other way. Plates on the left half of the screen mirror, so they all face the centre. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Shape")
	bool bMirror = false;

	/** Palette colour of the filled part (the whole plate for a plate). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Shape", meta = (Categories = "Palette"))
	FGameplayTag FillColorTag;

	/** Palette colour of the unfilled part. Unset = nothing drawn there, which is what a plate wants. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Shape", meta = (Categories = "Palette"))
	FGameplayTag TrackColorTag;

	/** Tint the fill for a moment, then settle back. */
	UFUNCTION(BlueprintCallable, Category = "HUD Shape")
	void Flash(FLinearColor Color, float Seconds = 0.35f);

	/** Replace the resting fill colour (the palette value is the default). */
	UFUNCTION(BlueprintCallable, Category = "HUD Shape")
	void SetFillColor(FLinearColor Color);

	/** Rest at the palette colour multiplied by Tint. The weapon plate takes its ammo colour this
	 *  way, the same as tinting a grey texture: the palette sets how light the plate is, the
	 *  tint says which hue. */
	UFUNCTION(BlueprintCallable, Category = "HUD Shape")
	void SetFillTint(FLinearColor Tint);

	/** The colour the fill rests at, from the palette unless SetFillColor changed it. */
	UFUNCTION(BlueprintPure, Category = "HUD Shape")
	FLinearColor GetFillColor() const { return FillFlash.Rest; }

	UFUNCTION(BlueprintCallable, Category = "HUD Shape")
	void SetMirror(bool bInMirror);

protected:

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Write every constant the material needs. Children add their own after calling this. */
	virtual void PushShapeParameters();

	UMaterialInstanceDynamic* GetShapeMaterial() const { return ShapeMaterial; }

	UPROPERTY(BlueprintReadOnly, Category = "HUD Shape", meta = (BindWidget))
	TObjectPtr<UImage> Shape;

private:

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShapeMaterial;

	FVector2D LastSize = FVector2D::ZeroVector;
	FHudColorFlash FillFlash;
};
