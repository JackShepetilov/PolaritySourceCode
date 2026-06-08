// BarEntryWidget.cpp

#include "BarEntryWidget.h"

void UBarEntryWidget::SetIcon(UTexture2D* InIcon)
{
	BP_SetIcon(InIcon);
}

void UBarEntryWidget::SetCount(int32 Count, bool bShow)
{
	BP_SetCount(Count, bShow);
}

void UBarEntryWidget::SetCooldown(float Remaining, float Total)
{
	CooldownTotal = FMath::Max(Total, 0.0f);
	CooldownRemaining = (CooldownTotal > 0.0f) ? FMath::Clamp(Remaining, 0.0f, CooldownTotal) : 0.0f;

	const float Norm = (CooldownTotal > 0.0f) ? CooldownRemaining / CooldownTotal : 0.0f;
	BP_SetCooldown(CooldownRemaining, CooldownTotal, Norm);
}

void UBarEntryWidget::PlayIntro()
{
	BP_PlayIntro();
}

void UBarEntryWidget::PlayFirstTimeIntro()
{
	BP_PlayFirstTimeIntro();
}

void UBarEntryWidget::SetKeybindHint(const FText& KeyText, UTexture2D* KeyIcon)
{
	BP_SetKeybindHint(KeyText, KeyIcon);
}

float UBarEntryWidget::PlayOutro()
{
	return BP_PlayOutro();
}

void UBarEntryWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Only do work while a cooldown is actively running.
	if (CooldownTotal <= 0.0f)
	{
		return;
	}

	CooldownRemaining = FMath::Max(CooldownRemaining - InDeltaTime, 0.0f);
	const float Norm = CooldownRemaining / CooldownTotal;
	BP_SetCooldown(CooldownRemaining, CooldownTotal, Norm);

	if (CooldownRemaining <= 0.0f)
	{
		CooldownTotal = 0.0f; // done — stop ticking
	}
}
