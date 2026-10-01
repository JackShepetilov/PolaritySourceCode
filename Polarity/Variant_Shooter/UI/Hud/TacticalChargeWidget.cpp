// TacticalChargeWidget.cpp

#include "Variant_Shooter/UI/Hud/TacticalChargeWidget.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Tactical/TacticalDeviceComponent.h"
#include "Variant_Shooter/Tactical/TacticalDeviceDefinition.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void UTacticalChargeWidget::BindCharacter(AShooterCharacter* Character)
{
	Device = Character ? Character->GetTacticalDeviceComponent() : nullptr;
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UTacticalChargeWidget::UnbindCharacter()
{
	Device = nullptr;
}

void UTacticalChargeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const UTacticalDeviceComponent* const Comp = Device.Get();
	const float Charge = Comp ? Comp->GetCharge() : -1.0f;
	const uint8 State = !Comp || !Comp->GetDevice() ? 0 : (Comp->IsDeviceRunning() ? 2 : 1);
	if (!FMath::IsNearlyEqual(Charge, LastCharge, 0.002f) || State != LastState)
	{
		LastCharge = Charge;
		LastState = State;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

int32 UTacticalChargeWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	const UTacticalDeviceComponent* const Comp = Device.Get();
	const UTacticalDeviceDefinition* const Def = Comp ? Comp->GetDevice() : nullptr;
	if (!Def)
	{
		return Layer;
	}

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	if (Size.X <= 1.0f || Size.Y <= 1.0f)
	{
		return Layer;
	}

	const FSlateBrush* const White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const float Charge = FMath::Clamp(Comp->GetCharge(), 0.0f, 1.0f);

	// Track.
	FSlateDrawElement::MakeBox(OutDrawElements, ++Layer, AllottedGeometry.ToPaintGeometry(), White,
		ESlateDrawEffect::None, TrackColor * InWidgetStyle.GetColorAndOpacityTint());

	// Fill: the device's colour, dim until it can switch on, bright while it runs.
	const FVector2D Inner(FMath::Max(0.0f, Size.X - FillInset * 2.0f), FMath::Max(0.0f, Size.Y - FillInset * 2.0f));
	FLinearColor Fill = Def->Color;
	Fill.A = Comp->IsDeviceRunning() ? 1.0f : (Comp->IsChargeReady() ? 0.8f : NotReadyAlpha);
	if (Charge > 0.0f)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, ++Layer,
			AllottedGeometry.ToPaintGeometry(FVector2f(Inner.X * Charge, Inner.Y), FSlateLayoutTransform(FVector2f(FillInset, FillInset))),
			White, ESlateDrawEffect::None, Fill * InWidgetStyle.GetColorAndOpacityTint());
	}

	// The switch-on line.
	if (Def->MinChargeToActivate > 0.0f)
	{
		const float X = FillInset + Inner.X * Def->MinChargeToActivate;
		FSlateDrawElement::MakeBox(OutDrawElements, ++Layer,
			AllottedGeometry.ToPaintGeometry(FVector2f(2.0f, Size.Y), FSlateLayoutTransform(FVector2f(X - 1.0f, 0.0f))),
			White, ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, 0.6f) * InWidgetStyle.GetColorAndOpacityTint());
	}

	return Layer;
}
