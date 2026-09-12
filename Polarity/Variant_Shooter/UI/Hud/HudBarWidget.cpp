// HudBarWidget.cpp

#include "HudBarWidget.h"

#include "Materials/MaterialInstanceDynamic.h"

namespace HudBarParams
{
	static const FName Fill(TEXT("Fill"));
	static const FName Lag(TEXT("Lag"));
	static const FName LagColor(TEXT("LagColor"));
	static const FName Segments(TEXT("Segments"));
}

UHudBarWidget::UHudBarWidget()
{
	FillColor = FLinearColor(1.0f, 0.31f, 0.29f, 1.0f);
	TrackColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.12f);
}

void UHudBarWidget::SetFraction(float Fraction, bool bInstant)
{
	const float New = FMath::Clamp(Fraction, 0.0f, 1.0f);
	if (bInstant)
	{
		Target = Fill = Lag = New;
		RiseElapsed = RiseTime;
		LagWait = 0.0f;
		PushFill();
		return;
	}
	if (FMath::IsNearlyEqual(New, Target, 0.0005f))
	{
		return;
	}
	if (New < Target)
	{
		// A drop: the fill tells the truth at once, the ghost keeps the old value for a beat.
		Lag = FMath::Max(Lag, Fill);
		Fill = New;
		RiseElapsed = RiseTime;
		LagWait = LagDelay;
	}
	else
	{
		// A rise: sweep the fill up; the ghost has nothing to say and rides on the fill.
		RiseFrom = Fill;
		RiseElapsed = 0.0f;
	}
	Target = New;
	PushFill();
}

void UHudBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	bool bChanged = false;
	if (RiseElapsed < RiseTime)
	{
		RiseElapsed = FMath::Min(RiseElapsed + InDeltaTime, RiseTime);
		Fill = FMath::Lerp(RiseFrom, Target, FMath::InterpEaseOut(0.0f, 1.0f, RiseElapsed / RiseTime, 2.0f));
		Lag = FMath::Max(Lag, Fill);
		bChanged = true;
	}
	if (Lag > Fill)
	{
		if (LagWait > 0.0f)
		{
			LagWait -= InDeltaTime;
		}
		else
		{
			Lag = FMath::Max(Fill, Lag - InDeltaTime / FMath::Max(LagDrainTime, 0.05f));
			bChanged = true;
		}
	}
	if (bChanged)
	{
		PushFill();
	}
}

void UHudBarWidget::PushShapeParameters()
{
	Super::PushShapeParameters();
	if (UMaterialInstanceDynamic* Mat = GetShapeMaterial())
	{
		Mat->SetVectorParameterValue(HudBarParams::LagColor, LagColor);
		Mat->SetScalarParameterValue(HudBarParams::Segments, static_cast<float>(Segments));
	}
	PushFill();
}

void UHudBarWidget::PushFill()
{
	if (UMaterialInstanceDynamic* Mat = GetShapeMaterial())
	{
		Mat->SetScalarParameterValue(HudBarParams::Fill, Fill);
		Mat->SetScalarParameterValue(HudBarParams::Lag, Lag);
	}
}
