// TutorialSlideWidget.cpp
// Fullscreen slide widget implementation

#include "TutorialSlideWidget.h"
#include "TutorialSubsystem.h"
#include "InputAction.h"
#include "Engine/GameInstance.h"

void UTutorialSlideWidget::SetupSlide(UTexture2D* InImage, const FText& InCloseText, UTexture2D* InCloseIcon, UInputAction* InCloseAction)
{
	SlideImage = InImage;
	CloseHintText = InCloseText;
	CloseKeyIcon = InCloseIcon;
	CloseAction = InCloseAction;

	// Get expected close key for input handling
	if (UTutorialSubsystem* Subsystem = GetTutorialSubsystem())
	{
		ExpectedCloseKey = Subsystem->GetFirstKeyForInputAction(CloseAction, GetOwningPlayer());
	}

	// Notify Blueprint
	BP_OnSlideSetup(SlideImage, CloseHintText, CloseKeyIcon);
}

void UTutorialSlideWidget::HideSlide()
{
	if (bIsHiding)
	{
		return;
	}

	bIsHiding = true;

	// Notify Blueprint to play animation
	BP_OnHideSlide();
}

void UTutorialSlideWidget::RequestClose()
{
	if (bIsHiding)
	{
		return;
	}

	if (UTutorialSubsystem* Subsystem = GetTutorialSubsystem())
	{
		Subsystem->CloseSlide(true);
	}
}

void UTutorialSlideWidget::OnHideAnimationFinished()
{
	RemoveFromParent();
}

void UTutorialSlideWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Make widget focusable for keyboard input
	SetIsFocusable(true);
}

void UTutorialSlideWidget::NativeDestruct()
{
	Super::NativeDestruct();
}

FReply UTutorialSlideWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Check if pressed key matches expected close key
	if (ExpectedCloseKey.IsValid() && InKeyEvent.GetKey() == ExpectedCloseKey)
	{
		RequestClose();
		return FReply::Handled();
	}

	// Also accept Enter/Space as universal close keys
	if (InKeyEvent.GetKey() == EKeys::Enter || 
		InKeyEvent.GetKey() == EKeys::SpaceBar ||
		InKeyEvent.GetKey() == EKeys::Escape)
	{
		RequestClose();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

UTutorialSubsystem* UTutorialSlideWidget::GetTutorialSubsystem() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UTutorialSubsystem>();
	}
	return nullptr;
}
