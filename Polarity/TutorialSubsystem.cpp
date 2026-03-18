// TutorialSubsystem.cpp
// Tutorial management subsystem implementation

#include "TutorialSubsystem.h"
#include "InputIconsDataAsset.h"
#include "TutorialHintWidget.h"
#include "TutorialSlideWidget.h"
#include "Variant_Shooter/UI/ShooterBulletCounterUI.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "ChargeAnimationComponent.h"
#include "Polarity.h"

// ==================== Arrow Input Processor ====================

/** Lightweight IInputProcessor that forwards key down/up to TutorialSubsystem for HUD arrow hold-to-close */
class FArrowInputProcessor : public IInputProcessor
{
public:
	FArrowInputProcessor(UTutorialSubsystem* InOwner) : Owner(InOwner) {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (Owner && Owner->IsHUDArrowActive() && !InKeyEvent.IsRepeat() && Owner->IsArrowCloseKey(InKeyEvent.GetKey()))
		{
			Owner->HandleArrowKeyDown(InKeyEvent);
			return true; // consume
		}
		return false;
	}

	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (Owner && Owner->IsHUDArrowActive() && Owner->IsArrowHolding() && Owner->IsArrowCloseKey(InKeyEvent.GetKey()))
		{
			Owner->HandleArrowKeyUp(InKeyEvent);
			return true; // consume
		}
		return false;
	}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override { return false; }
	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override { return false; }
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override { return false; }

private:
	UTutorialSubsystem* Owner = nullptr;
};

void UTutorialSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FWorldDelegates::OnWorldCleanup.AddUObject(this, &UTutorialSubsystem::OnWorldCleanup);

	UE_LOG(LogPolarity, Log, TEXT("TutorialSubsystem initialized"));
}

void UTutorialSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldCleanup.RemoveAll(this);

	UnbindArrowInput();

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

// ==================== FTickableGameObject ====================

void UTutorialSubsystem::Tick(float DeltaTime)
{
	// Handle HUD arrow hold-to-close progress (ticks while paused)
	if (!bHUDArrowActive || !bArrowHoldingCloseKey)
	{
		return;
	}

	ArrowCurrentHoldTime += FApp::GetDeltaTime();

	float Progress = ArrowHoldDuration > 0.0f ? FMath::Clamp(ArrowCurrentHoldTime / ArrowHoldDuration, 0.0f, 1.0f) : 1.0f;

	// Notify HUD of progress
	if (HUDWidget)
	{
		HUDWidget->BP_UpdateTutorialHoldProgress(Progress);
	}

	// Check if hold is complete
	if (Progress >= 1.0f)
	{
		bArrowHoldingCloseKey = false;
		CloseHUDArrow(true);
	}
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

void UTutorialSubsystem::SetHUDWidget(UShooterBulletCounterUI* InHUDWidget)
{
	HUDWidget = InHUDWidget;
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

	// Don't show if slide or arrow is active
	if (bSlideActive || bHUDArrowActive)
	{
		UE_LOG(LogPolarity, Warning, TEXT("Cannot show hint '%s' - a slide or arrow is active"), *TutorialID.ToString());
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

	// Check if multi-hint mode
	if (MutableHintData.bIsMultiHint && MutableHintData.MultiHintEntries.Num() > 0)
	{
		// Build multi-hint display data
		FMultiHintDisplayData MultiDisplayData = BuildMultiHintDisplayData(MutableHintData, PC);

		// Get primary action for completion (from first entry that has actions)
		UInputAction* PrimaryAction = nullptr;
		for (const FTutorialHintEntry& Entry : MutableHintData.MultiHintEntries)
		{
			if (Entry.InputActions.Num() > 0 && Entry.InputActions[0])
			{
				PrimaryAction = Entry.InputActions[0].Get();
				break;
			}
		}

		ActiveHintWidget->SetupMultiHint(MultiDisplayData, PrimaryAction);
	}
	else
	{
		// Standard single-hint mode
		FHintDisplayData DisplayData = BuildHintDisplayData(MutableHintData, PC);

		TArray<UInputAction*> RawActions;
		for (const TObjectPtr<UInputAction>& Action : MutableHintData.InputActions)
		{
			RawActions.Add(Action.Get());
		}

		ActiveHintWidget->SetupHintEx(DisplayData, RawActions);
	}

	ActiveHintWidget->AddToViewport(100); // High Z-order

	// Set state AFTER successful widget creation
	ActiveHintID = TutorialID;
	bHintActive = true;

	OnHintShown.Broadcast(TutorialID);

	UE_LOG(LogPolarity, Log, TEXT("Showing hint: %s (multi: %s)"),
		   *TutorialID.ToString(),
		   MutableHintData.bIsMultiHint ? TEXT("true") : TEXT("false"));

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

	// Don't show if HUD arrow is active
	if (bHUDArrowActive)
	{
		UE_LOG(LogPolarity, Warning, TEXT("Cannot show slide '%s' - HUD arrow is active"), *TutorialID.ToString());
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

	// Configure and show widget (with hold duration)
	ActiveSlideWidget->SetupSlide(SlideData.SlideImage, SlideData.CloseHintText, CloseIcon, SlideData.CloseAction, SlideData.HoldDuration);
	ActiveSlideWidget->AddToViewport(200); // Higher Z-order than hints

	ActiveSlideID = TutorialID;
	bSlideActive = true;

	// Cancel charge animation (channeling) on the player so they don't stay in the "arm extended" pose
	if (APawn* Pawn = PC->GetPawn())
	{
		if (AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(Pawn))
		{
			if (UChargeAnimationComponent* ChargeAnim = ShooterChar->GetChargeAnimationComponent())
			{
				ChargeAnim->CancelAnimation();
			}
		}
	}

	// Pause game
	UGameplayStatics::SetGamePaused(GetWorld(), true);

	// Set input mode to UI only
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(ActiveSlideWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);
	PC->SetShowMouseCursor(true);

	OnSlideShown.Broadcast(TutorialID);

	UE_LOG(LogPolarity, Log, TEXT("Showing slide: %s (hold: %.1fs)"), *TutorialID.ToString(), SlideData.HoldDuration);

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

	// Restore input mode BEFORE unpausing so the flush happens while paused
	if (PC)
	{
		FInputModeGameOnly InputMode;
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(false);

		// Flush all pressed keys so the close key (e.g. Space) doesn't trigger gameplay actions (e.g. Jump)
		PC->FlushPressedKeys();
	}

	// Unpause game (after flush so released keys don't fire gameplay actions)
	UGameplayStatics::SetGamePaused(GetWorld(), false);

	// Mark completed if requested
	if (bMarkCompleted)
	{
		MarkCompleted(CompletedID);
	}

	UE_LOG(LogPolarity, Log, TEXT("Closed slide: %s"), *CompletedID.ToString());
}

// ==================== HUD Arrow API ====================

bool UTutorialSubsystem::ShowHUDArrow(FName TutorialID, const FTutorialHUDArrowData& ArrowData, APlayerController* PlayerController)
{
	// Don't show if already completed
	if (IsCompleted(TutorialID))
	{
		return false;
	}

	// Don't show if another arrow or slide is active
	if (bHUDArrowActive || bSlideActive)
	{
		UE_LOG(LogPolarity, Warning, TEXT("Cannot show HUD arrow '%s' - another overlay is active"), *TutorialID.ToString());
		return false;
	}

	if (!HUDWidget)
	{
		UE_LOG(LogPolarity, Error, TEXT("Cannot show HUD arrow '%s' - HUD widget not set. Call SetHUDWidget() first."), *TutorialID.ToString());
		return false;
	}

	APlayerController* PC = GetPlayerController(PlayerController);
	if (!PC)
	{
		UE_LOG(LogPolarity, Error, TEXT("Cannot show HUD arrow - no valid PlayerController"));
		return false;
	}

	// Hide any active hint first
	if (bHintActive)
	{
		HideHint(false);
	}

	// Resolve close key icon
	UTexture2D* CloseIcon = GetIconForInputAction(ArrowData.CloseAction, PC);

	// Resolve expected close key
	ArrowExpectedCloseKey = GetFirstKeyForInputAction(ArrowData.CloseAction, PC);
	ArrowHoldDuration = FMath::Max(0.0f, ArrowData.HoldDuration);
	ArrowCurrentHoldTime = 0.0f;
	bArrowHoldingCloseKey = false;
	ActiveArrowElement = ArrowData.TargetElement;
	ActiveHUDArrowID = TutorialID;
	bHUDArrowActive = true;

	// Notify HUD widget
	HUDWidget->BP_ShowTutorialArrow(ArrowData.TargetElement, ArrowData.DescriptionText, CloseIcon, ArrowData.CloseHintText);

	// Pause game
	UGameplayStatics::SetGamePaused(GetWorld(), true);

	// Set input mode to Game and UI so the HUD (which is the game viewport widget) can still receive input
	// We use GameAndUI because the HUD is not a separate focusable widget like slides
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);
	PC->SetShowMouseCursor(true);

	// Bind Slate-level input for hold detection
	BindArrowInput();

	OnHUDArrowShown.Broadcast(TutorialID);

	UE_LOG(LogPolarity, Log, TEXT("Showing HUD arrow: %s -> %d (hold: %.1fs)"),
		   *TutorialID.ToString(), (int32)ArrowData.TargetElement, ArrowData.HoldDuration);

	return true;
}

void UTutorialSubsystem::CloseHUDArrow(bool bMarkCompleted)
{
	if (!bHUDArrowActive)
	{
		return;
	}

	FName CompletedID = ActiveHUDArrowID;

	// Unbind input
	UnbindArrowInput();

	// Notify HUD widget to hide arrow
	if (HUDWidget)
	{
		HUDWidget->BP_HideTutorialArrow(ActiveArrowElement);
	}

	APlayerController* PC = GetPlayerController(nullptr);

	// Reset state
	bHUDArrowActive = false;
	bArrowHoldingCloseKey = false;
	ActiveHUDArrowID = NAME_None;

	// Restore input mode and flush keys BEFORE unpausing
	if (PC)
	{
		FInputModeGameOnly InputMode;
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(false);
		PC->FlushPressedKeys();
	}

	// Unpause game
	UGameplayStatics::SetGamePaused(GetWorld(), false);

	// Mark completed if requested
	if (bMarkCompleted)
	{
		MarkCompleted(CompletedID);
	}

	UE_LOG(LogPolarity, Log, TEXT("Closed HUD arrow: %s"), *CompletedID.ToString());
}

void UTutorialSubsystem::RunTutorialDebugReveal(const TArray<FName>& TutorialIDsToComplete)
{
	UE_LOG(LogPolarity, Log, TEXT("TutorialSubsystem: Running debug reveal"));

	if (HUDWidget)
	{
		// Flash show+hide for each HUD element so the widget can make them permanently visible
		HUDWidget->BP_ShowTutorialArrow(EHUDElement::ChargeBar, FText::GetEmpty(), nullptr, FText::GetEmpty());
		HUDWidget->BP_HideTutorialArrow(EHUDElement::ChargeBar);

		HUDWidget->BP_ShowTutorialArrow(EHUDElement::HealthBar, FText::GetEmpty(), nullptr, FText::GetEmpty());
		HUDWidget->BP_HideTutorialArrow(EHUDElement::HealthBar);

		HUDWidget->BP_ShowTutorialArrow(EHUDElement::MeleeCharges, FText::GetEmpty(), nullptr, FText::GetEmpty());
		HUDWidget->BP_HideTutorialArrow(EHUDElement::MeleeCharges);
	}
	else
	{
		UE_LOG(LogPolarity, Warning, TEXT("TutorialSubsystem: Debug reveal - HUD widget not set yet"));
	}

	// Mark all specified tutorials as completed so they never trigger
	for (const FName& ID : TutorialIDsToComplete)
	{
		MarkCompleted(ID);
	}

	UE_LOG(LogPolarity, Log, TEXT("TutorialSubsystem: Debug reveal done, marked %d tutorials completed"), TutorialIDsToComplete.Num());
}

// ==================== HUD Arrow Input ====================

bool UTutorialSubsystem::IsArrowCloseKey(const FKey& Key) const
{
	if (ArrowExpectedCloseKey.IsValid() && Key == ArrowExpectedCloseKey)
	{
		return true;
	}
	return Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Escape;
}

void UTutorialSubsystem::BindArrowInput()
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	// Create and register input processor for key down/up handling
	ArrowInputProcessor = MakeShared<FArrowInputProcessor>(this);
	FSlateApplication::Get().RegisterInputPreProcessor(ArrowInputProcessor, 0);
}

void UTutorialSubsystem::UnbindArrowInput()
{
	if (ArrowInputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(ArrowInputProcessor);
	}
	ArrowInputProcessor.Reset();
}

void UTutorialSubsystem::HandleArrowKeyDown(const FKeyEvent& InKeyEvent)
{
	if (ArrowHoldDuration <= 0.0f)
	{
		CloseHUDArrow(true);
	}
	else
	{
		bArrowHoldingCloseKey = true;
		ArrowCurrentHoldTime = 0.0f;
		if (HUDWidget)
		{
			HUDWidget->BP_UpdateTutorialHoldProgress(0.0f);
		}
	}
}

void UTutorialSubsystem::HandleArrowKeyUp(const FKeyEvent& InKeyEvent)
{
	bArrowHoldingCloseKey = false;
	ArrowCurrentHoldTime = 0.0f;
	if (HUDWidget)
	{
		HUDWidget->BP_OnTutorialHoldCancelled();
	}
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
		UE_LOG(LogPolarity, Warning, TEXT("GetFirstKeyForInputAction: InputAction is null"));
		return EKeys::Invalid;
	}

	APlayerController* PC = GetPlayerController(PlayerController);
	if (!PC)
	{
		UE_LOG(LogPolarity, Warning, TEXT("GetFirstKeyForInputAction: No PlayerController for action '%s'"), *InputAction->GetName());
		return EKeys::Invalid;
	}

	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	if (!LocalPlayer)
	{
		UE_LOG(LogPolarity, Warning, TEXT("GetFirstKeyForInputAction: No LocalPlayer for action '%s'"), *InputAction->GetName());
		return EKeys::Invalid;
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!InputSubsystem)
	{
		UE_LOG(LogPolarity, Warning, TEXT("GetFirstKeyForInputAction: No EnhancedInputLocalPlayerSubsystem for action '%s'"), *InputAction->GetName());
		return EKeys::Invalid;
	}

	// Query keys mapped to this action
	TArray<FKey> MappedKeys = InputSubsystem->QueryKeysMappedToAction(InputAction);

	UE_LOG(LogPolarity, Log, TEXT("GetFirstKeyForInputAction: Action '%s' has %d mapped keys"), *InputAction->GetName(), MappedKeys.Num());
	for (const FKey& Key : MappedKeys)
	{
		UE_LOG(LogPolarity, Log, TEXT("  - Key: %s (gamepad: %s)"), *Key.ToString(), Key.IsGamepadKey() ? TEXT("yes") : TEXT("no"));
	}

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

	UE_LOG(LogPolarity, Warning, TEXT("GetFirstKeyForInputAction: No keys mapped for action '%s' - is the InputMappingContext active?"), *InputAction->GetName());
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

	UE_LOG(LogPolarity, Log, TEXT("GetIconsForInputActions: Resolving %d actions, InputIconsAsset: %s"),
		   InputActions.Num(),
		   InputIconsAsset ? *InputIconsAsset->GetName() : TEXT("NULL"));

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

				UE_LOG(LogPolarity, Log, TEXT("  Action '%s' -> Key '%s' -> Icon: %s"),
					   *Action->GetName(), *IconData.Key.ToString(),
					   IconData.Icon ? *IconData.Icon->GetName() : TEXT("NULL"));
			}
			else
			{
				UE_LOG(LogPolarity, Warning, TEXT("  Action '%s' -> Key INVALID (no mapping found)"), *Action->GetName());
			}
		}
		else
		{
			UE_LOG(LogPolarity, Warning, TEXT("  Action is NULL in InputActions array"));
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

FMultiHintDisplayData UTutorialSubsystem::BuildMultiHintDisplayData(const FTutorialHintData& HintData, APlayerController* PlayerController) const
{
	FMultiHintDisplayData Result;

	for (const FTutorialHintEntry& Entry : HintData.MultiHintEntries)
	{
		FMultiHintEntryDisplayData EntryData;
		EntryData.EntryText = Entry.EntryText;
		EntryData.bIsCombination = Entry.bIsCombination;
		EntryData.bHasIcons = false;

		// Resolve icons for this entry
		TArray<UInputAction*> RawActions;
		for (const TObjectPtr<UInputAction>& Action : Entry.InputActions)
		{
			RawActions.Add(Action.Get());
		}

		EntryData.Icons = GetIconsForInputActions(RawActions, PlayerController);

		for (const FTutorialInputIconData& IconData : EntryData.Icons)
		{
			if (IconData.bIsValid)
			{
				EntryData.bHasIcons = true;
				break;
			}
		}

		Result.Entries.Add(EntryData);
	}

	return Result;
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

	UnbindArrowInput();

	// Widgets are destroyed by the engine during world cleanup.
	// Null our pointers so they get recreated on the new level.
	ActiveHintWidget = nullptr;
	ActiveSlideWidget = nullptr;
	HUDWidget = nullptr;
	bHintActive = false;
	bSlideActive = false;
	bHUDArrowActive = false;
	bArrowHoldingCloseKey = false;
	ActiveHintID = NAME_None;
	ActiveSlideID = NAME_None;
	ActiveHUDArrowID = NAME_None;
}
