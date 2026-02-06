// SubtitleWidget.cpp

#include "SubtitleWidget.h"

void USubtitleWidget::ShowSubtitle(const FText& InText, const FText& InSpeaker, float InDuration)
{
	SubtitleText = InText;
	Speaker = InSpeaker;
	Duration = InDuration;
	bIsVisible = true;
	bIsHiding = false;

	// Call Blueprint implementation
	BP_OnShowSubtitle(InText, InSpeaker, InDuration);
}

void USubtitleWidget::HideSubtitle()
{
	if (bIsHiding)
	{
		return;
	}

	bIsHiding = true;

	// Call Blueprint implementation
	BP_OnHideSubtitle();
}

void USubtitleWidget::OnHideAnimationFinished()
{
	bIsVisible = false;
	bIsHiding = false;

	// Clear text
	SubtitleText = FText::GetEmpty();
	Speaker = FText::GetEmpty();
	Duration = 0.0f;
}
