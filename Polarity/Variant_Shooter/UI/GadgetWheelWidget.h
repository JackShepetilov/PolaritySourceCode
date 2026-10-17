// GadgetWheelWidget.h
// The gadget wheel: held H shows every kind carried around the centre of the screen, the look picks
// one, letting go of H selects it [author, 2026-10-16].
//
// Drawn in NativePaint, like the map, so it needs no blueprint: UGrenadeComponent creates it from this
// class (or a child set on the component) and feeds it what to show. It only shows; the choosing is the
// component's.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GadgetWheelWidget.generated.h"

/** One kind on the wheel. */
USTRUCT(BlueprintType)
struct FGadgetWheelEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Gadget Wheel")
	FText Label;

	UPROPERTY(BlueprintReadOnly, Category = "Gadget Wheel")
	int32 Count = 0;
};

UCLASS()
class POLARITY_API UGadgetWheelWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/** Where entry I sits, as a screen direction (x right, y down). The component picks by the same
	 *  directions, so what is drawn and what is chosen cannot disagree. */
	static FVector2D GetEntryDirection(int32 Index, int32 Num);

	void SetEntries(const TArray<FGadgetWheelEntry>& InEntries, int32 InPerKind, int32 InSelected);

	/** Hovered: INDEX_NONE for none. Cursor: the look so far, 0..1 of the pick distance. */
	void SetHover(int32 InHovered, const FVector2D& InCursor);

	/** From the centre of the screen to the centre of an entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gadget Wheel", meta = (ClampMin = "40.0"))
	float Radius = 170.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gadget Wheel")
	FVector2D EntrySize = FVector2D(190.0f, 64.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gadget Wheel")
	FLinearColor EntryColour = FLinearColor(0.02f, 0.02f, 0.02f, 0.75f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gadget Wheel")
	FLinearColor HoverColour = FLinearColor(0.85f, 0.55f, 0.1f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gadget Wheel")
	FLinearColor TextColour = FLinearColor::White;

	/** The kind on G now, marked under its name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gadget Wheel")
	FLinearColor SelectedMarkColour = FLinearColor(0.85f, 0.55f, 0.1f, 1.0f);

protected:

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:

	TArray<FGadgetWheelEntry> Entries;
	int32 PerKind = 0;
	int32 Selected = INDEX_NONE;
	int32 Hovered = INDEX_NONE;
	FVector2D LookOffset = FVector2D::ZeroVector;
};
