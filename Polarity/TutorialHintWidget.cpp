// TutorialHintWidget.cpp
// Compact hint widget implementation

#include "TutorialHintWidget.h"
#include "InputAction.h"

void UTutorialHintWidget::SetupHintEx(const FHintDisplayData& InDisplayData, const TArray<UInputAction*>& InInputActions)
{
	DisplayData = InDisplayData;

	// Copy input actions
	InputActions.Empty();
	for (UInputAction* Action : InInputActions)
	{
		if (Action)
		{
			InputActions.Add(Action);
		}
	}

	// Update deprecated fields for backward compatibility
	HintText = DisplayData.HintText;
	if (DisplayData.Icons.Num() > 0 && DisplayData.Icons[0].bIsValid)
	{
		KeyIcon = DisplayData.Icons[0].Icon;
	}
	if (InputActions.Num() > 0)
	{
		InputAction = InputActions[0];
	}

	// Notify Blueprint (new event)
	BP_OnHintSetupEx(DisplayData);

	// Also call legacy event for backward compatibility
	BP_OnHintSetup(HintText, KeyIcon.Get());
}

void UTutorialHintWidget::SetupHint(const FText& InText, UTexture2D* InIcon, UInputAction* InInputAction)
{
	// Build display data from legacy parameters
	FHintDisplayData LegacyDisplayData;
	LegacyDisplayData.HintText = InText;
	LegacyDisplayData.bIsCombination = false;

	if (InIcon)
	{
		FTutorialInputIconData IconData;
		IconData.Icon = InIcon;
		IconData.bIsValid = true;
		LegacyDisplayData.Icons.Add(IconData);
		LegacyDisplayData.bHasIcons = true;
	}

	TArray<UInputAction*> Actions;
	if (InInputAction)
	{
		Actions.Add(InInputAction);
	}

	SetupHintEx(LegacyDisplayData, Actions);
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

UInputAction* UTutorialHintWidget::GetInputAction() const
{
	return InputActions.Num() > 0 ? InputActions[0].Get() : nullptr;
}

UTexture2D* UTutorialHintWidget::GetKeyIcon() const
{
	if (DisplayData.Icons.Num() > 0 && DisplayData.Icons[0].bIsValid)
	{
		return DisplayData.Icons[0].Icon.Get();
	}
	return nullptr;
}
