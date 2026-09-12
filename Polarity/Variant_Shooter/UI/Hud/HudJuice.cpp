// HudJuice.cpp

#include "HudJuice.h"

#include "Components/TextBlock.h"

void FHudNumberTween::Set(int32 NewTarget, bool bInstant)
{
	if (bInstant)
	{
		Target = NewTarget;
		Displayed = static_cast<float>(NewTarget);
		RollElapsed = RollTime;
		Punch = 0.0f;
		return;
	}
	if (NewTarget == Target)
	{
		return;
	}
	Target = NewTarget;
	RollFrom = Displayed;
	RollElapsed = 0.0f;
	Punch = 1.0f;
}

void FHudNumberTween::Update(float DeltaTime, UTextBlock* Text)
{
	if (RollElapsed < RollTime)
	{
		RollElapsed = FMath::Min(RollElapsed + DeltaTime, RollTime);
		const float Alpha = FMath::InterpEaseOut(0.0f, 1.0f, RollElapsed / RollTime, 2.5f);
		Displayed = FMath::Lerp(RollFrom, static_cast<float>(Target), Alpha);
	}
	else
	{
		Displayed = static_cast<float>(Target);
	}

	if (Punch > 0.0f)
	{
		Punch = FMath::Max(0.0f, Punch - DeltaTime / FMath::Max(PunchTime, KINDA_SMALL_NUMBER));
	}

	if (Text)
	{
		Text->SetText(FText::AsNumber(Shown()));
		const float Scale = 1.0f + (PunchScale - 1.0f) * FMath::InterpEaseIn(0.0f, 1.0f, Punch, 2.0f);
		Text->SetRenderScale(FVector2D(Scale, Scale));
	}
}

void FHudColorFlash::Flash(const FLinearColor& Color, float Time)
{
	FlashColor = Color;
	FlashTime = FMath::Max(Time, KINDA_SMALL_NUMBER);
	Remaining = FlashTime;
}

FLinearColor FHudColorFlash::Update(float DeltaTime)
{
	if (Remaining <= 0.0f)
	{
		return Rest;
	}
	Remaining = FMath::Max(0.0f, Remaining - DeltaTime);
	const float Alpha = FMath::InterpEaseIn(0.0f, 1.0f, Remaining / FlashTime, 2.0f);
	return FLinearColor::LerpUsingHSV(Rest, FlashColor, Alpha);
}
