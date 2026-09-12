// HudShapeWidget.cpp

#include "HudShapeWidget.h"

#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PolarityPalette.h"

namespace HudShapeParams
{
	static const FName Size(TEXT("Size"));
	static const FName Lean(TEXT("Lean"));
	static const FName Radius(TEXT("Radius"));
	static const FName Mirror(TEXT("Mirror"));
	static const FName FillColor(TEXT("FillColor"));
	static const FName TrackColor(TEXT("TrackColor"));
}

void UHudShapeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// GetDynamicMaterial makes the instance from whatever material the brush holds and puts it
	// back into the brush, so the designer picks the material and C++ only sets numbers on it.
	ShapeMaterial = Shape ? Shape->GetDynamicMaterial() : nullptr;
	if (!ShapeMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("[HUD_DEBUG] %s: Shape has no material brush, nothing will be drawn"), *GetName());
	}
	// White for an unnamed fill reads as unstyled, the palette's own rule; an unset track is invisible.
	FillFlash.Rest = UPolarityPalette::GetColor(FillColorTag, FLinearColor::White);
	LastSize = FVector2D::ZeroVector;
	PushShapeParameters();
}

void UHudShapeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!ShapeMaterial)
	{
		return;
	}

	const FVector2D Size = MyGeometry.GetLocalSize();
	if (!Size.Equals(LastSize, 0.25f))
	{
		LastSize = Size;
		ShapeMaterial->SetVectorParameterValue(HudShapeParams::Size, FLinearColor(Size.X, Size.Y, 0.0f, 0.0f));
		const float LeanPx = LeanReferenceHeight > 0.0f ? Lean * Size.Y / LeanReferenceHeight : Lean;
		ShapeMaterial->SetScalarParameterValue(HudShapeParams::Lean, LeanPx);
	}
	if (FillFlash.Remaining > 0.0f)
	{
		ShapeMaterial->SetVectorParameterValue(HudShapeParams::FillColor, FillFlash.Update(InDeltaTime));
	}
}

void UHudShapeWidget::PushShapeParameters()
{
	if (!ShapeMaterial)
	{
		return;
	}
	ShapeMaterial->SetScalarParameterValue(HudShapeParams::Lean, Lean);
	ShapeMaterial->SetScalarParameterValue(HudShapeParams::Radius, Radius);
	ShapeMaterial->SetScalarParameterValue(HudShapeParams::Mirror, bMirror ? 1.0f : 0.0f);
	ShapeMaterial->SetVectorParameterValue(HudShapeParams::FillColor, FillFlash.Rest);
	ShapeMaterial->SetVectorParameterValue(HudShapeParams::TrackColor,
		UPolarityPalette::GetColor(TrackColorTag, FLinearColor::Transparent));
}

void UHudShapeWidget::Flash(FLinearColor Color, float Seconds)
{
	FillFlash.Flash(Color, Seconds);
}

void UHudShapeWidget::SetFillColor(FLinearColor Color)
{
	FillFlash.Rest = Color;
	if (ShapeMaterial && FillFlash.Remaining <= 0.0f)
	{
		ShapeMaterial->SetVectorParameterValue(HudShapeParams::FillColor, Color);
	}
}

void UHudShapeWidget::SetFillTint(FLinearColor Tint)
{
	const FLinearColor Base = UPolarityPalette::GetColor(FillColorTag, FLinearColor::White);
	SetFillColor(FLinearColor(Base.R * Tint.R, Base.G * Tint.G, Base.B * Tint.B, Base.A * Tint.A));
}

void UHudShapeWidget::SetMirror(bool bInMirror)
{
	bMirror = bInMirror;
	if (ShapeMaterial)
	{
		ShapeMaterial->SetScalarParameterValue(HudShapeParams::Mirror, bMirror ? 1.0f : 0.0f);
	}
}
