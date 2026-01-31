// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterKeyBindingsUI.h"
#include "EnhancedInputSubsystems.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"

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

FReply UShooterKeyBindingsUI::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsListeningForKey)
	{
		FKey PressedKey = InMouseEvent.GetEffectingButton();

		// Don't allow left click to be bound (used for UI interaction)
		// But allow other mouse buttons, and allow left click for Fire action
		if (PressedKey != EKeys::LeftMouseButton || ActionBeingRebound == FName("IA_Fire"))
		{
			ProcessKeyPress(PressedKey);
			return FReply::Handled();
		}
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
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

	// Store the Input Action pointer
	if (TObjectPtr<UInputAction>* FoundAction = ActionNameToInputAction.Find(ActionName))
	{
		ActionBeingReboundPtr = *FoundAction;
	}

	// Set keyboard focus to this widget
	SetKeyboardFocus();

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

	BP_StopKeyListening();
}

void UShooterKeyBindingsUI::ClearBinding(FName ActionName, bool bIsSecondary)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	if (!Subsystem)
	{
		return;
	}

	// Find the Input Action
	TObjectPtr<UInputAction>* FoundAction = ActionNameToInputAction.Find(ActionName);
	if (!FoundAction || !*FoundAction)
	{
		return;
	}

	// For clearing, we need to use PlayerMappableInputConfig or manual IMC manipulation
	// Since UE5's Enhanced Input doesn't have built-in "clear single binding" for user settings,
	// we'll handle this by storing custom bindings and rebuilding

	// For now, refresh the list to reflect any changes
	BuildKeyBindingsList();
	BP_RefreshBindingsList();
}

void UShooterKeyBindingsUI::ConfirmKeyConflict()
{
	if (!PendingConflictKey.IsValid() || ActionBeingRebound == NAME_None)
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	if (!Subsystem)
	{
		CancelKeyListening();
		return;
	}

	// Clear the conflicting binding first, then apply the new one
	// This requires more complex logic with EnhancedInputUserSettings
	// For now, just apply the new binding (which will override)

	// Update UI
	BuildKeyBindingsList();
	BP_OnKeyBindingChanged(ActionBeingRebound, PendingConflictKey, bIsRebindingSecondary);
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
	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	if (Subsystem)
	{
		// Reset all IMCs to their default mappings
		// This clears any player-specific remappings
		for (UInputMappingContext* IMC : InputMappingContexts)
		{
			if (IMC)
			{
				// Re-add the IMC to refresh its mappings
				// Priority 0 is default, adjust as needed
				Subsystem->RemoveMappingContext(IMC);
				Subsystem->AddMappingContext(IMC, 0);
			}
		}
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
	if (ActionBeingReboundPtr)
	{
		if (ApplyKeyBinding(ActionBeingReboundPtr, PressedKey, bIsRebindingSecondary))
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

		// Track which actions we've already added (for primary/secondary slot handling)
		TMap<const UInputAction*, int32> ActionMappingCount;

		for (const FEnhancedActionKeyMapping& Mapping : Mappings)
		{
			const UInputAction* Action = Mapping.Action.Get();
			if (!Action)
			{
				continue;
			}

			FName ActionName = FName(*Action->GetName());

			// Check if we already have an entry for this action
			int32* ExistingIndex = nullptr;
			for (int32 i = 0; i < CachedBindings.Num(); ++i)
			{
				if (CachedBindings[i].ActionName == ActionName)
				{
					ExistingIndex = &i;
					break;
				}
			}

			if (ExistingIndex)
			{
				// This is a secondary binding for an existing action
				int32& Count = ActionMappingCount.FindOrAdd(Action);
				if (Count == 1)
				{
					// Second mapping becomes secondary key
					CachedBindings[*ExistingIndex].SecondaryKey = Mapping.Key;
				}
				Count++;
			}
			else
			{
				// New action entry
				FKeyBindingDisplayInfo Info;
				Info.InputAction = const_cast<UInputAction*>(Action);
				Info.ActionName = ActionName;
				Info.DisplayName = GetActionDisplayName(Action);
				Info.Category = IMCCategoryName;
				Info.PrimaryKey = Mapping.Key;
				Info.SecondaryKey = EKeys::Invalid;
				Info.bCanRemap = true;

				CachedBindings.Add(Info);
				ActionNameToInputAction.Add(ActionName, const_cast<UInputAction*>(Action));
				ActionMappingCount.Add(Action, 1);
			}
		}
	}

	// Sort by category then by display name
	CachedBindings.Sort([](const FKeyBindingDisplayInfo& A, const FKeyBindingDisplayInfo& B)
	{
		int32 CategoryCompare = A.Category.CompareTo(B.Category);
		if (CategoryCompare != 0)
		{
			return CategoryCompare < 0;
		}
		return A.DisplayName.CompareTo(B.DisplayName) < 0;
	});
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

	// Try to get a nice display name from the action name
	// Remove "IA_" prefix if present and add spaces before capitals
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

bool UShooterKeyBindingsUI::ApplyKeyBinding(const UInputAction* Action, FKey NewKey, bool bIsSecondary)
{
	if (!Action)
	{
		return false;
	}

	UEnhancedInputUserSettings* UserSettings = GetEnhancedInputUserSettings();
	if (!UserSettings)
	{
		UE_LOG(LogTemp, Error, TEXT("ApplyKeyBinding: UserSettings is null. Make sure 'Enable User Settings' is checked in Project Settings -> Enhanced Input"));
		return false;
	}

	// Get the mapping name from the Input Action's PlayerMappableKeySettings
	FName MappingName = GetMappingNameForAction(Action);
	if (MappingName == NAME_None)
	{
		// Fallback to action name if no mapping name is set
		MappingName = FName(*Action->GetName());
		UE_LOG(LogTemp, Warning, TEXT("ApplyKeyBinding: No PlayerMappableKeySettings for action %s, using action name as mapping name"), *Action->GetName());
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

	// Save settings
	UserSettings->SaveSettings();

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

FName UShooterKeyBindingsUI::GetMappingNameForAction(const UInputAction* Action) const
{
	if (!Action)
	{
		return NAME_None;
	}

	// Check if the action has PlayerMappableKeySettings
	if (const UPlayerMappableKeySettings* KeySettings = Action->GetPlayerMappableKeySettings())
	{
		return KeySettings->Name;
	}

	return NAME_None;
}
