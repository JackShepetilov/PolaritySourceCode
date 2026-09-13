// HudJuice.cpp

#include "HudJuice.h"

#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"

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

// ==================== FHudDeltaStack ====================

void FHudDeltaStack::WriteValue(const FEntry& Entry) const
{
	UTextBlock* Text = Entry.Text.Get();
	if (!Text)
	{
		return;
	}
	// "%+d" spells the sign for both directions: the plus is the whole point of a gain number.
	Text->SetText(FText::FromString(FString::Printf(TEXT("%+d"), Entry.Value)));
	Text->SetColorAndOpacity(FSlateColor(Entry.Value >= 0 ? GainColor : LossColor));
}

void FHudDeltaStack::Push(int32 Delta, UPanelWidget* Panel, UObject* Outer)
{
	if (Delta == 0 || !Panel || !Outer)
	{
		return;
	}

	// Same sign, still holding: fold it in. The number grows and re-pops instead of a second one
	// appearing under it, which is what makes a pile of scrap read as one pickup.
	if (Entries.Num() > 0)
	{
		FEntry& Newest = Entries.Last();
		const bool bSameSign = (Newest.Value > 0) == (Delta > 0);
		if (bSameSign && Newest.Fading < 0.0f && Newest.Text.IsValid())
		{
			Newest.Value += Delta;
			Newest.Age = 0.0f;
			Newest.Pop = 1.0f;
			WriteValue(Newest);
			return;
		}
	}

	// Room for one more: the oldest leaves outright rather than fading, so the stack never shows
	// more than MaxEntries and a fast burst cannot pile up a column.
	while (Entries.Num() >= MaxEntries)
	{
		if (UTextBlock* Old = Entries[0].Text.Get())
		{
			Old->RemoveFromParent();
		}
		Entries.RemoveAt(0);
	}

	UTextBlock* Text = NewObject<UTextBlock>(Outer);
	Text->SetFont(Font);
	Text->SetShadowOffset(ShadowOffset);
	Text->SetShadowColorAndOpacity(ShadowColor);
	Text->SetVisibility(ESlateVisibility::HitTestInvisible);
	Text->SetRenderTransformPivot(FVector2D(0.5, 1.0));
	Panel->AddChild(Text);
	if (UVerticalBoxSlot* BoxSlot = Cast<UVerticalBoxSlot>(Text->Slot))
	{
		BoxSlot->SetHorizontalAlignment(HAlign_Center);
	}

	FEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Text = Text;
	Entry.Value = Delta;
	Entry.Pop = 1.0f;
	WriteValue(Entry);
}

void FHudDeltaStack::Update(float DeltaTime)
{
	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		FEntry& Entry = Entries[Index];
		UTextBlock* Text = Entry.Text.Get();
		if (!Text)
		{
			Entries.RemoveAt(Index);
			continue;
		}

		Entry.Age += DeltaTime;
		Entry.Pop = FMath::Max(0.0f, Entry.Pop - DeltaTime / FMath::Max(PopTime, KINDA_SMALL_NUMBER));

		if (Entry.Fading < 0.0f && Entry.Age >= HoldTime)
		{
			Entry.Fading = 0.0f;
		}

		float Opacity = 1.0f;
		float Lift = 0.0f;
		if (Entry.Fading >= 0.0f)
		{
			Entry.Fading += DeltaTime;
			const float Fade = FMath::Clamp(Entry.Fading / FMath::Max(FadeTime, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
			if (Fade >= 1.0f)
			{
				Text->RemoveFromParent();
				Entries.RemoveAt(Index);
				continue;
			}
			Opacity = 1.0f - FMath::InterpEaseIn(0.0f, 1.0f, Fade, 2.0f);
			Lift = -Rise * FMath::InterpEaseOut(0.0f, 1.0f, Fade, 2.0f);
		}

		const float Scale = 1.0f + (PopScale - 1.0f) * FMath::InterpEaseIn(0.0f, 1.0f, Entry.Pop, 2.0f);
		Text->SetRenderScale(FVector2D(Scale, Scale));
		Text->SetRenderTranslation(FVector2D(0.0, Lift));
		Text->SetRenderOpacity(Opacity);
	}
}

void FHudDeltaStack::Clear()
{
	for (FEntry& Entry : Entries)
	{
		if (UTextBlock* Text = Entry.Text.Get())
		{
			Text->RemoveFromParent();
		}
	}
	Entries.Empty();
}
