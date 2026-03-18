// TutorialSlideWidget.cpp
// Fullscreen slide widget implementation

#include "TutorialSlideWidget.h"
#include "TutorialSubsystem.h"
#include "InputAction.h"
#include "Engine/GameInstance.h"

void UTutorialSlideWidget::SetupSlide(UTexture2D* InImage, const FText& InCloseText, UTexture2D* InCloseIcon, UInputAction* InCloseAction, float InHoldDuration)
{
	SlideImage = InImage;
	CloseHintText = InCloseText;
	CloseKeyIcon = InCloseIcon;
	CloseAction = InCloseAction;
	HoldDuration = FMath::Max(0.0f, InHoldDuration);
	CurrentHoldTime = 0.0f;
	bIsHoldingCloseKey = false;

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

void UTutorialSlideWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (bIsHiding || !bIsHoldingCloseKey)
	{
		return;
	}

	// Accumulate hold time (use real time since game is paused)
	CurrentHoldTime += FApp::GetDeltaTime();

	float Progress = HoldDuration > 0.0f ? FMath::Clamp(CurrentHoldTime / HoldDuration, 0.0f, 1.0f) : 1.0f;

	// Notify Blueprint of progress
	BP_OnHoldProgressUpdated(Progress);

	// Check if hold is complete
	if (Progress >= 1.0f)
	{
		bIsHoldingCloseKey = false;
		RequestClose();
	}
}

FReply UTutorialSlideWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bIsHiding)
	{
		return FReply::Handled();
	}

	// Ignore repeat events (key already held)
	if (InKeyEvent.IsRepeat())
	{
		return FReply::Handled();
	}

	if (IsCloseKey(InKeyEvent.GetKey()))
	{
		if (HoldDuration <= 0.0f)
		{
			// Instant close (no hold required)
			RequestClose();
		}
		else
		{
			// Start hold
			bIsHoldingCloseKey = true;
			CurrentHoldTime = 0.0f;
			BP_OnHoldProgressUpdated(0.0f);
		}
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UTutorialSlideWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (IsCloseKey(InKeyEvent.GetKey()) && bIsHoldingCloseKey)
	{
		// Released before hold completed — cancel
		bIsHoldingCloseKey = false;
		CurrentHoldTime = 0.0f;
		BP_OnHoldCancelled();
		return FReply::Handled();
	}

	return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

bool UTutorialSlideWidget::IsCloseKey(const FKey& Key) const
{
	if (ExpectedCloseKey.IsValid() && Key == ExpectedCloseKey)
	{
		return true;
	}

	// Universal close keys
	return Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Escape;
}

UTutorialSubsystem* UTutorialSlideWidget::GetTutorialSubsystem() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UTutorialSubsystem>();
	}
	return nullptr;
}
