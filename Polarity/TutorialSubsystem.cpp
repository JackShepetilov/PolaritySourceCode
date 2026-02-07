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

	FWorldDelegates::OnWorldCleanup.AddUObject(this, &UTutorialSubsystem::OnWorldCleanup);

	UE_LOG(LogPolarity, Log, TEXT("TutorialSubsystem initialized"));
}

void UTutorialSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldCleanup.RemoveAll(this);

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
	// Validate configuration first
	FString ConfigError;
	if (!ValidateConfiguration(ConfigError))
	{
		UE_LOG(LogPolarity, Error, TEXT("ShowHint failed: %s"), *ConfigError);
		return false;
	}

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
		UE_LOG(LogPolarity, Error, TEXT("Cannot show hint '%s' - no valid PlayerController"), *TutorialID.ToString());
		return false;
	}

	// Create mutable copy for potential migration
	FTutorialHintData MutableHintData = HintData;
	MigrateHintDataIfNeeded(MutableHintData);

	// Create widget
	ActiveHintWidget = CreateWidget<UTutorialHintWidget>(PC, HintWidgetClass);
	if (!ActiveHintWidget)
	{
		UE_LOG(LogPolarity, Error, TEXT("Failed to create hint widget for '%s'"), *TutorialID.ToString());
		return false;
	}

	// Build display data with resolved icons
	FHintDisplayData DisplayData = BuildHintDisplayData(MutableHintData, PC);

	// Convert TObjectPtr array to raw pointer array for function call
	TArray<UInputAction*> RawActions;
	for (const TObjectPtr<UInputAction>& Action : MutableHintData.InputActions)
	{
		RawActions.Add(Action.Get());
	}

	// Configure and show widget
	ActiveHintWidget->SetupHintEx(DisplayData, RawActions);
	ActiveHintWidget->AddToViewport(100); // High Z-order

	// Set state AFTER successful widget creation
	ActiveHintID = TutorialID;
	bHintActive = true;

	OnHintShown.Broadcast(TutorialID);

	UE_LOG(LogPolarity, Log, TEXT("Showing hint: %s (icons: %d, combination: %s)"),
		   *TutorialID.ToString(),
		   DisplayData.Icons.Num(),
		   DisplayData.bIsCombination ? TEXT("true") : TEXT("false"));

	return true;
}

void UTutorialSubsystem::HideHint(bool bMarkCompleted)
{
	if (!bHintActive)
	{
		return;
	}

	FName CompletedID = ActiveHintID;

	// Reset state FIRST to prevent re-entry issues
	bHintActive = false;
	ActiveHintID = NAME_None;

	// Hide widget if valid
	if (ActiveHintWidget)
	{
		ActiveHintWidget->HideHint();
		ActiveHintWidget = nullptr;
	}

	// Mark completed if requested
	if (bMarkCompleted && !CompletedID.IsNone())
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

TArray<FTutorialInputIconData> UTutorialSubsystem::GetIconsForInputActions(const TArray<UInputAction*>& InputActions, APlayerController* PlayerController) const
{
	TArray<FTutorialInputIconData> Result;

	for (UInputAction* Action : InputActions)
	{
		FTutorialInputIconData IconData;

		if (Action)
		{
			IconData.Key = GetFirstKeyForInputAction(Action, PlayerController);

			if (IconData.Key.IsValid())
			{
				IconData.Icon = GetIconForKey(IconData.Key);
				IconData.bIsValid = (IconData.Icon != nullptr);
			}
		}

		Result.Add(IconData); // Add even if invalid to maintain alignment
	}

	return Result;
}

FHintDisplayData UTutorialSubsystem::BuildHintDisplayData(const FTutorialHintData& HintData, APlayerController* PlayerController) const
{
	FHintDisplayData DisplayData;
	DisplayData.HintText = HintData.HintText;
	DisplayData.bIsCombination = HintData.bIsCombination;
	DisplayData.bHasIcons = false;

	// Convert TObjectPtr to raw pointers for the function call
	TArray<UInputAction*> RawActions;
	for (const TObjectPtr<UInputAction>& Action : HintData.InputActions)
	{
		RawActions.Add(Action.Get());
	}

	// Get icons for all input actions
	DisplayData.Icons = GetIconsForInputActions(RawActions, PlayerController);

	// Check if we have any valid icons
	for (const FTutorialInputIconData& IconData : DisplayData.Icons)
	{
		if (IconData.bIsValid)
		{
			DisplayData.bHasIcons = true;
			break;
		}
	}

	return DisplayData;
}

void UTutorialSubsystem::MigrateHintDataIfNeeded(FTutorialHintData& HintData)
{
	// If old single InputAction is set but array is empty, migrate
	if (HintData.InputAction_DEPRECATED && HintData.InputActions.Num() == 0)
	{
		HintData.InputActions.Add(HintData.InputAction_DEPRECATED);
		UE_LOG(LogPolarity, Warning, TEXT("Migrated deprecated InputAction to InputActions array"));
	}
}

bool UTutorialSubsystem::ValidateConfiguration(FString& OutError) const
{
	if (!HintWidgetClass)
	{
		OutError = TEXT("HintWidgetClass not set. Call SetWidgetClasses() first.");
		return false;
	}

	if (!InputIconsAsset)
	{
		// Warning only - icons will be null but hint can still show text
		UE_LOG(LogPolarity, Warning, TEXT("InputIconsAsset not set - icons will not be displayed"));
	}

	return true;
}

void UTutorialSubsystem::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	// Only care about game/PIE worlds
	if (!World || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
	{
		return;
	}

	UE_LOG(LogPolarity, Log, TEXT("TutorialSubsystem: World cleanup - resetting widget state"));

	// Widgets are destroyed by the engine during world cleanup.
	// Null our pointers so they get recreated on the new level.
	ActiveHintWidget = nullptr;
	ActiveSlideWidget = nullptr;
	bHintActive = false;
	bSlideActive = false;
	ActiveHintID = NAME_None;
	ActiveSlideID = NAME_None;
}
