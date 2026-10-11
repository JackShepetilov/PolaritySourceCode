// HudHoldWidget.cpp

#include "HudHoldWidget.h"
#include "Components/TextBlock.h"
#include "HudBarWidget.h"
#include "HudShapeWidget.h"
#include "Variant_Shooter/ShooterCharacter.h"

void UHudHoldWidget::NativeBind(AShooterCharacter* Character)
{
	bShown = false;
	ShownKey = FText::GetEmpty();
	ShownLabel = FText::GetEmpty();

	// The content is hidden, never this widget: a collapsed UserWidget is not ticked.
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UWidget* const Content = GetRootWidget())
	{
		Content->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UHudHoldWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	FText Key;
	FText Label;
	float Progress = 0.0f;
	const AShooterCharacter* const Character = GetBoundCharacter();
	const bool bHolding = Character && Character->GetHoldPrompt(Key, Label, Progress);

	if (bHolding != bShown)
	{
		bShown = bHolding;
		if (UWidget* const Content = GetRootWidget())
		{
			Content->SetVisibility(bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		}
	}
	if (!bHolding)
	{
		return;
	}

	if (KeyText && !Key.EqualTo(ShownKey))
	{
		ShownKey = Key;
		KeyText->SetText(Key);
	}
	if (LabelText && !Label.EqualTo(ShownLabel))
	{
		ShownLabel = Label;
		LabelText->SetText(Label);
	}
	if (ProgressBar)
	{
		// Instant: the hold moves every frame by itself, and the bar's own sweep would trail it.
		ProgressBar->SetFraction(Progress, true);
	}
}
