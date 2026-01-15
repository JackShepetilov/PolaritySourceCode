// TutorialHintWidget.cpp
// Compact hint widget implementation

#include "TutorialHintWidget.h"

void UTutorialHintWidget::SetupHint(const FText& InText, UTexture2D* InIcon, UInputAction* InInputAction)
{
	HintText = InText;
	KeyIcon = InIcon;
	InputAction = InInputAction;

	// Notify Blueprint
	BP_OnHintSetup(HintText, KeyIcon);
}

void UTutorialHintWidget::HideHint()
{
	if (bIsHiding)
	{
		return;
	}

	bIsHiding = true;

	// Notify Blueprint to play animation
	BP_OnHideHint();
}

void UTutorialHintWidget::OnHideAnimationFinished()
{
	RemoveFromParent();
}
