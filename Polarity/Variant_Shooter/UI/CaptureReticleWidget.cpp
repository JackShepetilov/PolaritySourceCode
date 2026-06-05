// CaptureReticleWidget.cpp

#include "CaptureReticleWidget.h"

void UCaptureReticleWidget::UpdateForTarget(const FVector2D& ScreenPosition, float TargetPixelRadius, uint8 Polarity)
{
	// Size the brackets to ~the target's on-screen diameter, then express that as a render scale
	// relative to the design pixel size. Clamp so it never blows up at point-blank range or
	// disappears in the distance.
	const float DesiredDiameter = FMath::Max(TargetPixelRadius * 2.0f * BracketPadding, 1.0f);
	const float Scale = FMath::Clamp(DesiredDiameter / FMath::Max(ReferenceSize, 1.0f), MinScale, MaxScale);

	SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	FWidgetTransform T;
	T.Scale = FVector2D(Scale, Scale);
	SetRenderTransform(T);

	// Position the widget's center at the projected target center.
	SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
	SetPositionInViewport(ScreenPosition, true);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	if (!bHasTarget)
	{
		bHasTarget = true;
		BP_OnTargetChanged(true);
	}

	if (Polarity != LastPolarity)
	{
		LastPolarity = Polarity;
		BP_OnPolarityChanged(Polarity);
	}
}

void UCaptureReticleWidget::ClearTarget()
{
	if (bHasTarget)
	{
		bHasTarget = false;
		BP_OnTargetChanged(false);
	}
	SetVisibility(ESlateVisibility::Collapsed);
}
