// TutorialSubsystem.cpp
// Tutorial management subsystem implementation

#include "TutorialSubsystem.h"
#include "InputIconsDataAsset.h"
#include "TutorialHintWidget.h"
#include "TutorialSlideWidget.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Polarity.h"

void UTutorialSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogPolarity, Log, TEXT("TutorialSubsystem initialized"));
}

void UTutorialSubsystem::Deinitialize()
{
	// Clean up any active widgets
	if (ActiveHintWidget)
	{
		ActiveHintWidget->RemoveFromParent();
		ActiveHintWidget = nullptr;
	}

	if (ActiveSlideWidget)
	{
		ActiveSlideWidget->RemoveFromParent();
		ActiveSlideWidget = nullptr;
	}

	Super::Deinitialize();
}

// ==================== Configuration ====================

void UTutorialSubsystem::SetInputIconsAsset(UInputIconsDataAsset* InAsset)
{
	InputIconsAsset = InAsset;
}

void UTutorialSubsystem::SetWidgetClasses(TSubclassOf<UTutorialHintWidget> HintClass, TSubclassOf<UTutorialSlideWidget> SlideClass)
{
	HintWidgetClass = HintClass;
	SlideWidgetClass = SlideClass;
}

// ==================== Hint API ====================

bool UTutorialSubsystem::ShowHint(FName TutorialID, const FTutorialHintData& HintData, APlayerController* PlayerController)
{
	// Don't show if already completed
	if (IsCompleted(TutorialID))
	{
		return false;
	}

	// Don't show if hint already active
	if (bHintActive)
	{
		UE_LOG(LogPolarity, Warning, TEXT("Cannot show hint '%s' - another hint is active"), *TutorialID.ToString());
		return false;
	}

	// Don't show if slide is active
	if (bSlideActive)
	{
		UE_LOG(LogPolarity, Warning, TEXT("Cannot show hint '%s' - a slide is active"), *TutorialID.ToString());
		return false;
	}

	APlayerController* PC = GetPlayerController(PlayerController);
	if (!PC)
	{
		UE_LOG(LogPolarity, Error, TEXT("Cannot show hint - no valid PlayerController"));
		return false;
	}

	if (!HintWidgetClass)
	{
		UE_LOG(LogPolarity, Error, TEXT("Cannot show hint - HintWidgetClass not set"));
		return false;
	}

	// Create widget
	ActiveHintWidget = CreateWidget<UTutorialHintWidget>(PC, HintWidgetClass);
	if (!ActiveHintWidget)
	{
		UE_LOG(LogPolarity, Error, TEXT("Failed to create hint widget"));
		return false;
	}

	// Get icon for input action
	UTexture2D* Icon = GetIconForInputAction(HintData.InputAction, PC);

	// Configure and show widget
	ActiveHintWidget->SetupHint(HintData.HintText, Icon, HintData.InputAction);
	ActiveHintWidget->AddToViewport(100); // High Z-order

	ActiveHintID = TutorialID;
	bHintActive = true;

	OnHintShown.Broadcast(TutorialID);

	UE_LOG(LogPolarity, Log, TEXT("Showing hint: %s"), *TutorialID.ToString());

	return true;
}

void UTutorialSubsystem::HideHint(bool bMarkCompleted)
{
	if (!bHintActive || !ActiveHintWidget)
	{
		return;
	}

	FName CompletedID = ActiveHintID;

	// Hide widget
	ActiveHintWidget->HideHint();
	ActiveHintWidget = nullptr;

	bHintActive = false;
	ActiveHintID = NAME_None;

	// Mark completed if requested
	if (bMarkCompleted)
	{
		MarkCompleted(CompletedID);
	}

	UE_LOG(LogPolarity, Log, TEXT("Hidden hint: %s"), *CompletedID.ToString());
}

// ==================== Slide API ====================

bool UTutorialSubsystem::ShowSlide(FName TutorialID, const FTutorialSlideData& SlideData, APlayerController* PlayerController)
{
	// Don't show if already completed
	if (IsCompleted(TutorialID))
	{
		return false;
	}

	// Don't show if slide already active
	if (bSlideActive)
	{
		UE_LOG(LogPolarity, Warning, TEXT("Cannot show slide '%s' - another slide is active"), *TutorialID.ToString());
		return false;
	}

	APlayerController* PC = GetPlayerController(PlayerController);
	if (!PC)
	{
		UE_LOG(LogPolarity, Error, TEXT("Cannot show slide - no valid PlayerController"));
		return false;
	}

	if (!SlideWidgetClass)
	{
		UE_LOG(LogPolarity, Error, TEXT("Cannot show slide - SlideWidgetClass not set"));
		return false;
	}

	// Hide any active hint first
	if (bHintActive)
	{
		HideHint(false);
	}

	// Create widget
	ActiveSlideWidget = CreateWidget<UTutorialSlideWidget>(PC, SlideWidgetClass);
	if (!ActiveSlideWidget)
	{
		UE_LOG(LogPolarity, Error, TEXT("Failed to create slide widget"));
		return false;
	}

	// Get icon for close action
	UTexture2D* CloseIcon = GetIconForInputAction(SlideData.CloseAction, PC);

	// Configure and show widget
	ActiveSlideWidget->SetupSlide(SlideData.SlideImage, SlideData.CloseHintText, CloseIcon, SlideData.CloseAction);
	ActiveSlideWidget->AddToViewport(200); // Higher Z-order than hints

	ActiveSlideID = TutorialID;
	bSlideActive = true;

	// Pause game
	UGameplayStatics::SetGamePaused(GetWorld(), true);

	// Set input mode to UI only
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(ActiveSlideWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);
	PC->SetShowMouseCursor(true);

	OnSlideShown.Broadcast(TutorialID);

	UE_LOG(LogPolarity, Log, TEXT("Showing slide: %s"), *TutorialID.ToString());

	return true;
}

void UTutorialSubsystem::CloseSlide(bool bMarkCompleted)
{
	if (!bSlideActive || !ActiveSlideWidget)
	{
		return;
	}

	FName CompletedID = ActiveSlideID;

	APlayerController* PC = GetPlayerController(nullptr);

	// Hide widget
	ActiveSlideWidget->HideSlide();
	ActiveSlideWidget = nullptr;

	bSlideActive = false;
	ActiveSlideID = NAME_None;

	// Unpause game
	UGameplayStatics::SetGamePaused(GetWorld(), false);

	// Restore input mode
	if (PC)
	{
		FInputModeGameOnly InputMode;
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(false);
	}

	// Mark completed if requested
	if (bMarkCompleted)
	{
		MarkCompleted(CompletedID);
	}

	UE_LOG(LogPolarity, Log, TEXT("Closed slide: %s"), *CompletedID.ToString());
}

// ==================== Completion Tracking ====================

void UTutorialSubsystem::MarkCompleted(FName TutorialID)
{
	if (!TutorialID.IsNone() && !CompletedTutorials.Contains(TutorialID))
	{
		CompletedTutorials.Add(TutorialID);
		OnTutorialCompleted.Broadcast(TutorialID);

		UE_LOG(LogPolarity, Log, TEXT("Tutorial completed: %s"), *TutorialID.ToString());
	}
}

bool UTutorialSubsystem::IsCompleted(FName TutorialID) const
{
	return CompletedTutorials.Contains(TutorialID);
}

void UTutorialSubsystem::ResetCompletion(FName TutorialID)
{
	CompletedTutorials.Remove(TutorialID);
}

void UTutorialSubsystem::ResetAllProgress()
{
	CompletedTutorials.Empty();

	UE_LOG(LogPolarity, Log, TEXT("All tutorial progress reset"));
}

// ==================== Input Icon Lookup ====================

UTexture2D* UTutorialSubsystem::GetIconForInputAction(const UInputAction* InputAction, APlayerController* PlayerController) const
{
	FKey Key = GetFirstKeyForInputAction(InputAction, PlayerController);
	return GetIconForKey(Key);
}

UTexture2D* UTutorialSubsystem::GetIconForKey(const FKey& Key) const
{
	if (!InputIconsAsset)
	{
		UE_LOG(LogPolarity, Warning, TEXT("InputIconsAsset not set - cannot look up icon for key"));
		return nullptr;
	}

	return InputIconsAsset->GetIconForKey(Key);
}

FKey UTutorialSubsystem::GetFirstKeyForInputAction(const UInputAction* InputAction, APlayerController* PlayerController) const
{
	if (!InputAction)
	{
		return EKeys::Invalid;
	}

	APlayerController* PC = GetPlayerController(PlayerController);
	if (!PC)
	{
		return EKeys::Invalid;
	}

	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	if (!LocalPlayer)
	{
		return EKeys::Invalid;
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!InputSubsystem)
	{
		return EKeys::Invalid;
	}

	// Query keys mapped to this action
	TArray<FKey> MappedKeys = InputSubsystem->QueryKeysMappedToAction(InputAction);

	if (MappedKeys.Num() > 0)
	{
		// Return first key (prefer keyboard over gamepad if both exist)
		for (const FKey& Key : MappedKeys)
		{
			if (!Key.IsGamepadKey())
			{
				return Key;
			}
		}
		// Fallback to first key if all are gamepad
		return MappedKeys[0];
	}

	return EKeys::Invalid;
}

// ==================== Internal ====================

APlayerController* UTutorialSubsystem::GetPlayerController(APlayerController* Provided) const
{
	if (Provided)
	{
		return Provided;
	}

	// Get first local player controller
	UWorld* World = GetWorld();
	if (World)
	{
		return World->GetFirstPlayerController();
	}

	return nullptr;
}
