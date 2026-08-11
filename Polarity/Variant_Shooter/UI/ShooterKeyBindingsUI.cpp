// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterKeyBindingsUI.h"
#include "EnhancedInputSubsystems.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Framework/Application/SlateApplication.h"

void UShooterKeyBindingsUI::NativeConstruct()
{
	Super::NativeConstruct();

	bIsListeningForKey = false;
	ActionBeingRebound = NAME_None;
	ActionBeingReboundPtr = nullptr;
	bIsRebindingSecondary = false;
	PendingConflictKey = EKeys::Invalid;
	ConflictingActionName = NAME_None;

	// If no IMCs are set in Blueprint, try to get them from the PlayerController
	if (InputMappingContexts.Num() == 0)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem())
		{
			// Note: There's no direct way to get all IMCs from the subsystem
			// They should be configured in the Blueprint widget defaults
			UE_LOG(LogTemp, Warning, TEXT("ShooterKeyBindingsUI: No InputMappingContexts configured. Please set them in the Blueprint defaults."));
		}
	}

	// NOTE: We do NOT call RegisterInputMappingContexts here!
	// It corrupts Vector2D mappings (like IA_Move) causing all directions to map to one.
	// Instead, IMCs should be registered ONCE at game startup in your GameMode or PlayerController.
	//
	// To enable key remapping, add this to your PlayerController's BeginPlay:
	//   if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	//   {
	//       if (UEnhancedInputUserSettings* UserSettings = Subsystem->GetUserSettings())
	//       {
	//           TSet<UInputMappingContext*> Contexts;
	//           Contexts.Add(YourIMC);
	//           UserSettings->RegisterInputMappingContexts(Contexts);
	//       }
	//   }
	if (InputMappingContexts.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("ShooterKeyBindingsUI: Using %d Input Mapping Contexts (registration should happen in PlayerController)"), InputMappingContexts.Num());
	}

	BuildKeyBindingsList();

	BP_OnKeyBindingsOpened();
}

void UShooterKeyBindingsUI::NativeDestruct()
{
	BP_OnKeyBindingsClosed();
	Super::NativeDestruct();
}

FReply UShooterKeyBindingsUI::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bIsListeningForKey)
	{
		FKey PressedKey = InKeyEvent.GetKey();

		// Escape cancels key listening
		if (PressedKey == EKeys::Escape)
		{
			CancelKeyListening();
			return FReply::Handled();
		}

		ProcessKeyPress(PressedKey);
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UShooterKeyBindingsUI::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Consume key up events while listening to prevent them from propagating
	if (bIsListeningForKey)
	{
		return FReply::Handled();
	}
	return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

FReply UShooterKeyBindingsUI::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsListeningForKey)
	{
		FKey PressedKey = InMouseEvent.GetEffectingButton();

		// Don't allow left click to be bound (used for UI interaction)
		// But allow other mouse buttons, and allow left click for Fire action
		if (PressedKey != EKeys::LeftMouseButton || ActionBeingRebound == FName("Fire"))
		{
			ProcessKeyPress(PressedKey);
			return FReply::Handled();
		}
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UShooterKeyBindingsUI::NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent)
{
	UE_LOG(LogTemp, Log, TEXT("ShooterKeyBindingsUI: Focus received"));
	return Super::NativeOnFocusReceived(InGeometry, InFocusEvent);
}

// ==================== Key Binding Data ====================

TArray<FKeyBindingDisplayInfo> UShooterKeyBindingsUI::GetAllKeyBindings()
{
	// Rebuild if cache is empty
	if (CachedBindings.Num() == 0)
	{
		BuildKeyBindingsList();
	}
	return CachedBindings;
}

TArray<FKeyBindingDisplayInfo> UShooterKeyBindingsUI::GetBindingsForCategory(const FText& Category) const
{
	TArray<FKeyBindingDisplayInfo> FilteredBindings;

	for (const FKeyBindingDisplayInfo& Info : CachedBindings)
	{
		if (Info.Category.EqualTo(Category))
		{
			FilteredBindings.Add(Info);
		}
	}

	return FilteredBindings;
}

TArray<FText> UShooterKeyBindingsUI::GetAllCategories() const
{
	TArray<FText> Categories;
	TSet<FString> SeenCategories;

	for (const FKeyBindingDisplayInfo& Info : CachedBindings)
	{
		FString CategoryStr = Info.Category.ToString();
		if (!SeenCategories.Contains(CategoryStr))
		{
			SeenCategories.Add(CategoryStr);
			Categories.Add(Info.Category);
		}
	}

	return Categories;
}

// ==================== Key Binding Actions ====================

void UShooterKeyBindingsUI::StartListeningForKey(FName ActionName, bool bIsSecondary)
{
	bIsListeningForKey = true;
	ActionBeingRebound = ActionName;
	bIsRebindingSecondary = bIsSecondary;
	ActionBeingReboundPtr = nullptr;

	// Store the Input Action pointer
	if (TObjectPtr<UInputAction>* FoundAction = ActionNameToInputAction.Find(ActionName))
	{
		ActionBeingReboundPtr = *FoundAction;
		UE_LOG(LogTemp, Log, TEXT("StartListeningForKey: Found Input Action for '%s'"), *ActionName.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("StartListeningForKey: Could NOT find Input Action for '%s'! Available actions:"), *ActionName.ToString());
		for (const auto& Pair : ActionNameToInputAction)
		{
			UE_LOG(LogTemp, Warning, TEXT("  - %s"), *Pair.Key.ToString());
		}
	}

	// CRITICAL: Set input mode to Game and UI and focus this widget
	// This ensures keyboard events are routed to the widget
	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
	}

	// Also try direct focus
	SetKeyboardFocus();

	UE_LOG(LogTemp, Log, TEXT("StartListeningForKey: Now listening for key press for action '%s' (secondary: %s), ActionPtr valid: %s"),
		*ActionName.ToString(), bIsSecondary ? TEXT("true") : TEXT("false"),
		ActionBeingReboundPtr ? TEXT("YES") : TEXT("NO"));

	BP_StartKeyListening(ActionName, bIsSecondary);
}

void UShooterKeyBindingsUI::CancelKeyListening()
{
	bIsListeningForKey = false;
	ActionBeingRebound = NAME_None;
	ActionBeingReboundPtr = nullptr;
	bIsRebindingSecondary = false;
	PendingConflictKey = EKeys::Invalid;
	ConflictingActionName = NAME_None;

	// Restore normal UI input mode
	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
	}

	BP_StopKeyListening();
}

void UShooterKeyBindingsUI::ClearBinding(FName ActionName, bool bIsSecondary)
{
	// Public Blueprint API keeps the historical ActionName parameter, but it now carries
	// the stable Player Mappable mapping name.
	if (!ActionNameToInputAction.Contains(ActionName))
	{
		return;
	}

	// Clear the binding using EnhancedInputUserSettings
	ClearBindingInternal(ActionName, bIsSecondary);

	// Update cached binding
	for (FKeyBindingDisplayInfo& Info : CachedBindings)
	{
		if (Info.ActionName == ActionName)
		{
			if (bIsSecondary)
			{
				Info.SecondaryKey = EKeys::Invalid;
			}
			else
			{
				Info.PrimaryKey = EKeys::Invalid;
			}
			break;
		}
	}

	// Apply and save
	if (UEnhancedInputUserSettings* UserSettings = GetEnhancedInputUserSettings())
	{
		UserSettings->ApplySettings();
		UserSettings->SaveSettings();
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem())
	{
		Subsystem->RequestRebuildControlMappings();
	}

	BP_RefreshBindingsList();
}

void UShooterKeyBindingsUI::ConfirmKeyConflict()
{
	if (!PendingConflictKey.IsValid() || ActionBeingRebound == NAME_None)
	{
		return;
	}

	// First, clear the conflicting action's binding
	if (ConflictingActionName != NAME_None)
	{
		// Find which slot has the conflict and clear that exact mapping row.
		for (FKeyBindingDisplayInfo& Info : CachedBindings)
		{
			if (Info.ActionName == ConflictingActionName)
			{
				if (Info.PrimaryKey == PendingConflictKey)
				{
					ClearBindingInternal(ConflictingActionName, false);
					Info.PrimaryKey = EKeys::Invalid;
				}
				else if (Info.SecondaryKey == PendingConflictKey)
				{
					ClearBindingInternal(ConflictingActionName, true);
					Info.SecondaryKey = EKeys::Invalid;
				}
				break;
			}
		}
	}

	// Now apply the new binding
	if (ActionBeingRebound != NAME_None)
	{
		if (ApplyKeyBinding(ActionBeingRebound, PendingConflictKey, bIsRebindingSecondary))
		{
			UpdateCachedBinding(ActionBeingRebound, PendingConflictKey, bIsRebindingSecondary);
			BP_OnKeyBindingChanged(ActionBeingRebound, PendingConflictKey, bIsRebindingSecondary);
		}
	}

	BP_RefreshBindingsList();

	// Reset state
	CancelKeyListening();
}

void UShooterKeyBindingsUI::CancelKeyConflict()
{
	PendingConflictKey = EKeys::Invalid;
	ConflictingActionName = NAME_None;

	CancelKeyListening();
}

void UShooterKeyBindingsUI::ResetAllToDefaults()
{
	UEnhancedInputUserSettings* UserSettings = GetEnhancedInputUserSettings();
	if (UserSettings)
	{
		// Reset the current key profile to defaults
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		FGameplayTag CurrentProfileId = UserSettings->GetCurrentKeyProfileIdentifier();
		FGameplayTagContainer FailureReason;
		UserSettings->ResetKeyProfileToDefault(CurrentProfileId, FailureReason);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		if (!FailureReason.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("ResetAllToDefaults: Some keys failed to reset: %s"), *FailureReason.ToString());
		}

		UserSettings->ApplySettings();
		UserSettings->SaveSettings();
	}

	// Also request rebuild of control mappings
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem())
	{
		Subsystem->RequestRebuildControlMappings();
	}

	BuildKeyBindingsList();
	BP_RefreshBindingsList();
}

void UShooterKeyBindingsUI::ResetBindingToDefault(FName ActionName)
{
	// Similar to ResetAllToDefaults but for a single action
	// This is more complex and would require tracking original bindings
	BuildKeyBindingsList();
	BP_RefreshBindingsList();
}

void UShooterKeyBindingsUI::ApplyKeyBindings()
{
	// Key bindings are applied immediately in this implementation
	// This method is for compatibility if we add deferred application later
	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	if (Subsystem)
	{
		// Request a rebuild of input mappings
		Subsystem->RequestRebuildControlMappings();
	}
}

void UShooterKeyBindingsUI::CloseMenu()
{
	OnKeyBindingsMenuClosed.Broadcast();
	RemoveFromParent();
}

// ==================== Utility ====================

FText UShooterKeyBindingsUI::GetKeyDisplayName(FKey Key)
{
	if (!Key.IsValid())
	{
		return NSLOCTEXT("KeyBindings", "NotBound", "Not Bound");
	}

	return Key.GetDisplayName();
}

bool UShooterKeyBindingsUI::IsKeyValidForBinding(FKey Key)
{
	// Reserved keys that cannot be rebound
	static TArray<FKey> ReservedKeys = {
		EKeys::LeftCommand,
		EKeys::RightCommand,
		EKeys::Pause,
		EKeys::Tilde  // Console key
	};

	return Key.IsValid() && !ReservedKeys.Contains(Key);
}

// ==================== Protected Methods ====================

UEnhancedInputLocalPlayerSubsystem* UShooterKeyBindingsUI::GetEnhancedInputSubsystem() const
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
		{
			return LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		}
	}
	return nullptr;
}

void UShooterKeyBindingsUI::ProcessKeyPress(FKey PressedKey)
{
	if (!bIsListeningForKey || ActionBeingRebound == NAME_None)
	{
		return;
	}

	// Validate the key
	if (!IsKeyValidForBinding(PressedKey))
	{
		return;
	}

	// Check for conflicts
	FName ConflictAction;
	if (FindKeyConflict(PressedKey, ActionBeingRebound, ConflictAction))
	{
		// Store pending conflict and notify Blueprint
		PendingConflictKey = PressedKey;
		ConflictingActionName = ConflictAction;

		BP_OnKeyConflict(PressedKey, ActionBeingRebound, ConflictAction);
		return;
	}

	// Apply the new key binding using EnhancedInputUserSettings
	if (ActionBeingRebound != NAME_None)
	{
		if (ApplyKeyBinding(ActionBeingRebound, PressedKey, bIsRebindingSecondary))
		{
			// Update cached bindings
			UpdateCachedBinding(ActionBeingRebound, PressedKey, bIsRebindingSecondary);

			// Notify Blueprint
			BP_OnKeyBindingChanged(ActionBeingRebound, PressedKey, bIsRebindingSecondary);
			BP_RefreshBindingsList();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to apply key binding for action: %s"), *ActionBeingRebound.ToString());
		}
	}

	// Reset listening state
	CancelKeyListening();
}

void UShooterKeyBindingsUI::BuildKeyBindingsList()
{
	CachedBindings.Empty();
	ActionNameToInputAction.Empty();

	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	UEnhancedInputUserSettings* UserSettings = GetEnhancedInputUserSettings();
	TSet<const UInputAction*> SkippedActions;
	TMap<FName, int32> MappingCount;

	// NOTE: IMC registration moved to NativeConstruct (runs once per widget instance)
	// to avoid corrupting input mappings on repeated BuildKeyBindingsList calls
	if (!UserSettings)
	{
		UE_LOG(LogTemp, Error, TEXT("ShooterKeyBindingsUI: EnhancedInputUserSettings is NULL! "
			"Make sure 'Enable User Settings' is checked in Project Settings -> Enhanced Input, "
			"or add bEnableUserSettings=True to DefaultInput.ini under [/Script/EnhancedInput.EnhancedInputDeveloperSettings]"));
	}

	// Iterate through all configured Input Mapping Contexts
	for (UInputMappingContext* IMC : InputMappingContexts)
	{
		if (!IMC)
		{
			continue;
		}

		FText IMCCategoryName = GetCategoryFromIMC(IMC);

		// Get all mappings from this IMC
		const TArray<FEnhancedActionKeyMapping>& Mappings = IMC->GetMappings();

		for (const FEnhancedActionKeyMapping& Mapping : Mappings)
		{
			const UInputAction* Action = Mapping.Action.Get();
			if (!Action)
			{
				continue;
			}
			// GetPlayerMappableKeySettings resolves mapping-level Override Settings, action-level
			// inherited settings, and Ignore Settings in the same way Enhanced Input does.
			const UPlayerMappableKeySettings* KeySettings = Mapping.GetPlayerMappableKeySettings();
			if (!KeySettings)
			{
				continue;
			}
			const FName MappingName = Mapping.GetMappingName();
			if (MappingName.IsNone())
			{
				if (!SkippedActions.Contains(Action))
				{
					SkippedActions.Add(Action);
					UE_LOG(LogTemp, Warning,
						TEXT("ShooterKeyBindingsUI: Skipping mapping for '%s': effective Player Mappable settings are missing or Mapping Name is None."),
						*Action->GetName());
				}
				continue;
			}

			// Check if we already have an entry for this action
			int32 ExistingIndex = INDEX_NONE;
			for (int32 i = 0; i < CachedBindings.Num(); ++i)
			{
				if (CachedBindings[i].ActionName == MappingName)
				{
					ExistingIndex = i;
					break;
				}
			}

			if (ExistingIndex != INDEX_NONE)
			{
				// This is a secondary binding for an existing action
				int32& Count = MappingCount.FindOrAdd(MappingName);
				if (Count == 1)
				{
					// Second mapping becomes secondary key
					CachedBindings[ExistingIndex].SecondaryKey = Mapping.Key;
				}
				Count++;
			}
			else
			{
				// New action entry
				FKeyBindingDisplayInfo Info;
				Info.InputAction = const_cast<UInputAction*>(Action);
				Info.ActionName = MappingName;
				Info.DisplayName = KeySettings->DisplayName.IsEmpty()
					? GetActionDisplayName(Action)
					: KeySettings->DisplayName;
				Info.Category = KeySettings->DisplayCategory.IsEmpty()
					? IMCCategoryName
					: KeySettings->DisplayCategory;
				Info.PrimaryKey = Mapping.Key;
				Info.SecondaryKey = EKeys::Invalid;
				Info.bCanRemap = true;

				CachedBindings.Add(Info);
				ActionNameToInputAction.Add(MappingName, const_cast<UInputAction*>(Action));
				MappingCount.Add(MappingName, 1);
			}
		}
	}

	// Override with custom key mappings from UserSettings (player's remapped keys)
	if (UserSettings)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (UEnhancedPlayerMappableKeyProfile* KeyProfile = UserSettings->GetCurrentKeyProfile())
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		{
			// Get all player-mapped keys from the current profile
			const TMap<FName, FKeyMappingRow>& PlayerMappedRows = KeyProfile->GetPlayerMappingRows();

			for (FKeyBindingDisplayInfo& Info : CachedBindings)
			{
				// Check if there's a custom mapping for this action
				if (const FKeyMappingRow* MappingRow = PlayerMappedRows.Find(Info.ActionName))
				{
					// Mappings is a TSet<FPlayerKeyMapping>
					const TSet<FPlayerKeyMapping>& Mappings = MappingRow->Mappings;

					// Reset keys - we'll fill them from UserSettings
					Info.PrimaryKey = EKeys::Invalid;
					Info.SecondaryKey = EKeys::Invalid;

					for (const FPlayerKeyMapping& PlayerMapping : Mappings)
					{
						// Get the current key (which might be remapped)
						FKey CurrentKey = PlayerMapping.GetCurrentKey();

						if (PlayerMapping.GetSlot() == EPlayerMappableKeySlot::First)
						{
							Info.PrimaryKey = CurrentKey;
						}
						else if (PlayerMapping.GetSlot() == EPlayerMappableKeySlot::Second)
						{
							Info.SecondaryKey = CurrentKey;
						}
					}
				}
			}
		}
	}

	// Intentionally preserve designer-authored order: InputMappingContexts array order first,
	// then the first occurrence of each Mapping Name in the IMC mappings array.
}

bool UShooterKeyBindingsUI::FindKeyConflict(FKey Key, FName ExcludeAction, FName& OutConflictingAction) const
{
	for (const FKeyBindingDisplayInfo& Info : CachedBindings)
	{
		if (Info.ActionName == ExcludeAction)
		{
			continue;
		}

		if (Info.PrimaryKey == Key || Info.SecondaryKey == Key)
		{
			OutConflictingAction = Info.ActionName;
			return true;
		}
	}

	return false;
}

FText UShooterKeyBindingsUI::GetActionDisplayName(const UInputAction* Action) const
{
	if (!Action)
	{
		return FText::GetEmpty();
	}

	// Player Mappable Key Settings are the authoritative presentation metadata.
	if (const UPlayerMappableKeySettings* KeySettings = Action->GetPlayerMappableKeySettings())
	{
		if (!KeySettings->DisplayName.IsEmpty())
		{
			return KeySettings->DisplayName;
		}
	}

	// Legacy fallback for diagnostics/content that has not been migrated yet.
	FString ActionName = Action->GetName();

	// Remove common prefixes
	ActionName.RemoveFromStart(TEXT("IA_"));
	ActionName.RemoveFromStart(TEXT("InputAction_"));

	// Add spaces before capital letters (simple camel case to display name)
	FString DisplayName;
	for (int32 i = 0; i < ActionName.Len(); ++i)
	{
		TCHAR Char = ActionName[i];
		if (i > 0 && FChar::IsUpper(Char) && !FChar::IsUpper(ActionName[i - 1]))
		{
			DisplayName.AppendChar(TEXT(' '));
		}
		DisplayName.AppendChar(Char);
	}

	return FText::FromString(DisplayName);
}

FText UShooterKeyBindingsUI::GetCategoryFromIMC(const UInputMappingContext* IMC) const
{
	if (!IMC)
	{
		return NSLOCTEXT("KeyBindings", "General", "General");
	}

	// Get category from IMC name
	// Remove "IMC_" prefix if present
	FString IMCName = IMC->GetName();
	IMCName.RemoveFromStart(TEXT("IMC_"));

	// Common naming patterns
	if (IMCName.Contains(TEXT("Combat")) || IMCName.Contains(TEXT("Weapon")))
	{
		return NSLOCTEXT("KeyBindings", "Combat", "Combat");
	}
	if (IMCName.Contains(TEXT("Movement")) || IMCName.Contains(TEXT("Locomotion")))
	{
		return NSLOCTEXT("KeyBindings", "Movement", "Movement");
	}
	if (IMCName.Contains(TEXT("UI")) || IMCName.Contains(TEXT("Menu")))
	{
		return NSLOCTEXT("KeyBindings", "Interface", "Interface");
	}
	if (IMCName.Contains(TEXT("Vehicle")))
	{
		return NSLOCTEXT("KeyBindings", "Vehicle", "Vehicle");
	}

	// Default: use the IMC name as the category
	return FText::FromString(IMCName);
}

UEnhancedInputUserSettings* UShooterKeyBindingsUI::GetEnhancedInputUserSettings() const
{
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem())
	{
		return Subsystem->GetUserSettings();
	}
	return nullptr;
}

bool UShooterKeyBindingsUI::ApplyKeyBinding(FName MappingName, FKey NewKey, bool bIsSecondary)
{
	if (MappingName.IsNone())
	{
		return false;
	}

	UEnhancedInputUserSettings* UserSettings = GetEnhancedInputUserSettings();
	if (!UserSettings)
	{
		UE_LOG(LogTemp, Error, TEXT("ApplyKeyBinding: UserSettings is null. Make sure 'Enable User Settings' is checked in Project Settings -> Enhanced Input"));
		return false;
	}

	// Set up the mapping args
	FMapPlayerKeyArgs Args;
	Args.MappingName = MappingName;
	Args.NewKey = NewKey;
	Args.Slot = bIsSecondary ? EPlayerMappableKeySlot::Second : EPlayerMappableKeySlot::First;

	// Apply the mapping
	FGameplayTagContainer FailureReason;
	UserSettings->MapPlayerKey(Args, FailureReason);

	if (!FailureReason.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyKeyBinding failed for %s: %s"), *MappingName.ToString(), *FailureReason.ToString());
		return false;
	}

	// Apply and save settings
	UserSettings->ApplySettings();
	UserSettings->SaveSettings();

	// Request rebuild of input mappings in the subsystem
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem())
	{
		Subsystem->RequestRebuildControlMappings();
	}

	UE_LOG(LogTemp, Log, TEXT("Successfully remapped %s to %s (slot: %s)"),
		*MappingName.ToString(),
		*NewKey.ToString(),
		bIsSecondary ? TEXT("Secondary") : TEXT("Primary"));

	return true;
}

void UShooterKeyBindingsUI::UpdateCachedBinding(FName ActionName, FKey NewKey, bool bIsSecondary)
{
	for (FKeyBindingDisplayInfo& Info : CachedBindings)
	{
		if (Info.ActionName == ActionName)
		{
			if (bIsSecondary)
			{
				Info.SecondaryKey = NewKey;
			}
			else
			{
				Info.PrimaryKey = NewKey;
			}
			break;
		}
	}
}

void UShooterKeyBindingsUI::ClearBindingInternal(FName MappingName, bool bIsSecondary)
{
	if (MappingName.IsNone())
	{
		return;
	}

	UEnhancedInputUserSettings* UserSettings = GetEnhancedInputUserSettings();
	if (!UserSettings)
	{
		return;
	}

	// Use UnMapPlayerKey to clear the binding
	FMapPlayerKeyArgs Args;
	Args.MappingName = MappingName;
	Args.Slot = bIsSecondary ? EPlayerMappableKeySlot::Second : EPlayerMappableKeySlot::First;

	FGameplayTagContainer FailureReason;
	UserSettings->UnMapPlayerKey(Args, FailureReason);

	if (FailureReason.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("Cleared binding for %s (slot: %s)"),
			*MappingName.ToString(),
			bIsSecondary ? TEXT("Secondary") : TEXT("Primary"));
	}
}
