// GadgetWheelWidget.cpp

#include "Variant_Shooter/UI/GadgetWheelWidget.h"

#include "Fonts/SlateFontInfo.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

FVector2D UGadgetWheelWidget::GetEntryDirection(int32 Index, int32 Num)
{
	// From the left, clockwise on the screen: two kinds sit left and right; three sit left, upper right
	// and lower right.
	const float Degrees = 180.0f + 360.0f * static_cast<float>(Index) / static_cast<float>(FMath::Max(1, Num));
	const float Radians = FMath::DegreesToRadians(Degrees);
	return FVector2D(FMath::Cos(Radians), FMath::Sin(Radians));
}

void UGadgetWheelWidget::SetEntries(const TArray<FGadgetWheelEntry>& InEntries, int32 InPerKind, int32 InSelected)
{
	Entries = InEntries;
	PerKind = InPerKind;
	Selected = InSelected;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UGadgetWheelWidget::SetHover(int32 InHovered, const FVector2D& InCursor)
{
	Hovered = InHovered;
	LookOffset = InCursor;
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 UGadgetWheelWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	const FVector2D Centre = AllottedGeometry.GetLocalSize() * 0.5f;
	const FSlateBrush* const White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FSlateFontInfo NameFont = FCoreStyle::GetDefaultFontStyle("Bold", 16);
	const FSlateFontInfo CountFont = FCoreStyle::GetDefaultFontStyle("Regular", 12);

	auto Box = [&](const FVector2D& TopLeft, const FVector2D& Size, const FLinearColor& Colour, int32 OnLayer)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, OnLayer,
			AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)),
			White, ESlateDrawEffect::None, Colour);
	};
	auto Text = [&](const FVector2D& TopLeft, const FString& String, const FSlateFontInfo& Font, int32 OnLayer)
	{
		FSlateDrawElement::MakeText(OutDrawElements, OnLayer,
			AllottedGeometry.ToPaintGeometry(EntrySize, FSlateLayoutTransform(TopLeft)),
			String, Font, ESlateDrawEffect::None, TextColour);
	};

	++Layer;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FVector2D At = Centre + GetEntryDirection(Index, Entries.Num()) * Radius - EntrySize * 0.5f;
		Box(At, EntrySize, Index == Hovered ? HoverColour : EntryColour, Layer);
		if (Index == Selected)
		{
			Box(At + FVector2D(0.0f, EntrySize.Y - 4.0f), FVector2D(EntrySize.X, 4.0f), SelectedMarkColour, Layer + 1);
		}
		Text(At + FVector2D(12.0f, 8.0f), Entries[Index].Label.ToString(), NameFont, Layer + 1);
		Text(At + FVector2D(12.0f, 36.0f), FString::Printf(TEXT("x%d / %d"), Entries[Index].Count, PerKind), CountFont, Layer + 1);
	}

	// Where the look points: a dot that leaves the centre towards what it will pick.
	const float Dot = 10.0f;
	const FVector2D DotAt = Centre + LookOffset.GetClampedToMaxSize(1.0f) * Radius * 0.45f - FVector2D(Dot, Dot) * 0.5f;
	Box(DotAt, FVector2D(Dot, Dot), TextColour, Layer + 2);

	return Layer + 2;
}
