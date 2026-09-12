// HudShapeWidget.h
// The HUD's one shape: a leaning plate drawn by M_HudShape at whatever size the widget has.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HudJuice.h"
#include "HudShapeWidget.generated.h"

class UImage;
class UMaterialInstanceDynamic;

/**
 * Inherit in Blueprint (WBP_HudPlate) with an Image named Shape whose brush is M_HudShape. The
 * material draws a rounded parallelogram in pixel space, so the widget only has to tell it its
 * size (every tick, when it changes) and the colours. Plates put content in a Named Slot on top;
 * bars are the UHudBarWidget child.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UHudShapeWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/** How far the top edge leads the bottom one, in slate units at 1080p. 30 is the agreed look. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Shape", meta = (ClampMin = "0"))
	float Lean = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Shape", meta = (ClampMin = "0"))
	float Radius = 8.0f;

	/** Lean the other way. Plates on the right half of the screen mirror, so they all face the centre. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Shape")
	bool bMirror = false;

	/** Colour of the filled part (the whole plate for a plate). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Shape")
	FLinearColor FillColor = FLinearColor(0.05f, 0.06f, 0.08f, 0.85f);

	/** Colour of the unfilled part. Alpha 0 for a plate, a faint track for a bar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD Shape")
	FLinearColor TrackColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	/** Tint the fill for a moment, then settle back. */
	UFUNCTION(BlueprintCallable, Category = "HUD Shape")
	void Flash(FLinearColor Color, float Seconds = 0.35f);

	UFUNCTION(BlueprintCallable, Category = "HUD Shape")
	void SetFillColor(FLinearColor Color);

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
