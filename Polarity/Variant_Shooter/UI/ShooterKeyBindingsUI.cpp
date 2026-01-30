// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterKeyBindingsUI.h"
#include "Variant_Shooter/ShooterGameSettings.h"
#include "Framework/Application/SlateApplication.h"

TMap<FName, FKeyBindingDisplayInfo> UShooterKeyBindingsUI::ActionDisplayInfoMap;

void UShooterKeyBindingsUI::InitializeActionDisplayInfoMap()
{
	if (ActionDisplayInfoMap.Num() > 0)
	{
		return; // Already initialized
	}

	// Movement category
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_Jump");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "Jump", "Jump");
		Info.Category = NSLOCTEXT("KeyBindings", "Movement", "Movement");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_Crouch");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "Crouch", "Crouch / Slide");
		Info.Category = NSLOCTEXT("KeyBindings", "Movement", "Movement");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_Sprint");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "Sprint", "Sprint");
		Info.Category = NSLOCTEXT("KeyBindings", "Movement", "Movement");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_Dash");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "Dash", "Air Dash");
		Info.Category = NSLOCTEXT("KeyBindings", "Movement", "Movement");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}

	// Combat category
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_Fire");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "Fire", "Fire Weapon");
		Info.Category = NSLOCTEXT("KeyBindings", "Combat", "Combat");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_ADS");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "ADS", "Aim Down Sights");
		Info.Category = NSLOCTEXT("KeyBindings", "Combat", "Combat");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_Reload");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "Reload", "Reload");
		Info.Category = NSLOCTEXT("KeyBindings", "Combat", "Combat");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_Melee");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "Melee", "Melee Attack");
		Info.Category = NSLOCTEXT("KeyBindings", "Combat", "Combat");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_SwitchPolarity");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "SwitchPolarity", "Switch EMF Polarity");
		Info.Category = NSLOCTEXT("KeyBindings", "Combat", "Combat");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}

	// Weapons category
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_Weapon1");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "Weapon1", "Weapon Slot 1");
		Info.Category = NSLOCTEXT("KeyBindings", "Weapons", "Weapons");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_Weapon2");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "Weapon2", "Weapon Slot 2");
		Info.Category = NSLOCTEXT("KeyBindings", "Weapons", "Weapons");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_Weapon3");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "Weapon3", "Weapon Slot 3");
		Info.Category = NSLOCTEXT("KeyBindings", "Weapons", "Weapons");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_NextWeapon");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "NextWeapon", "Next Weapon");
		Info.Category = NSLOCTEXT("KeyBindings", "Weapons", "Weapons");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_PrevWeapon");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "PrevWeapon", "Previous Weapon");
		Info.Category = NSLOCTEXT("KeyBindings", "Weapons", "Weapons");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}

	// UI category
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_Pause");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "Pause", "Pause Menu");
		Info.Category = NSLOCTEXT("KeyBindings", "UI", "Interface");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}
	{
		FKeyBindingDisplayInfo Info;
		Info.ActionName = FName("IA_Scoreboard");
		Info.DisplayName = NSLOCTEXT("KeyBindings", "Scoreboard", "Scoreboard");
		Info.Category = NSLOCTEXT("KeyBindings", "UI", "Interface");
		Info.bCanRemap = true;
		ActionDisplayInfoMap.Add(Info.ActionName, Info);
	}
}

void UShooterKeyBindingsUI::NativeConstruct()
{
	Super::NativeConstruct();

	InitializeActionDisplayInfoMap();

	bIsListeningForKey = false;
	ActionBeingRebound = NAME_None;
	bIsRebindingSecondary = false;

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
		// But allow other mouse buttons
		if (PressedKey != EKeys::LeftMouseButton || ActionBeingRebound == FName("IA_Fire"))
		{
			ProcessKeyPress(PressedKey);
			return FReply::Handled();
		}
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

// ==================== Key Binding Data ====================

TArray<FKeyBindingDisplayInfo> UShooterKeyBindingsUI::GetAllKeyBindings() const
{
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

	Categories.Add(NSLOCTEXT("KeyBindings", "Movement", "Movement"));
	Categories.Add(NSLOCTEXT("KeyBindings", "Combat", "Combat"));
	Categories.Add(NSLOCTEXT("KeyBindings", "Weapons", "Weapons"));
	Categories.Add(NSLOCTEXT("KeyBindings", "UI", "Interface"));

	return Categories;
}

// ==================== Key Binding Actions ====================

void UShooterKeyBindingsUI::StartListeningForKey(FName ActionName, bool bIsSecondary)
{
	bIsListeningForKey = true;
	ActionBeingRebound = ActionName;
	bIsRebindingSecondary = bIsSecondary;

	// Set keyboard focus to this widget
	SetKeyboardFocus();

	BP_StartKeyListening(ActionName, bIsSecondary);
}

void UShooterKeyBindingsUI::CancelKeyListening()
{
	bIsListeningForKey = false;
	ActionBeingRebound = NAME_None;
	bIsRebindingSecondary = false;
	PendingConflictKey = EKeys::Invalid;
	ConflictingActionName = NAME_None;

	BP_StopKeyListening();
}

void UShooterKeyBindingsUI::ClearBinding(FName ActionName, bool bIsSecondary)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->ClearKeyBinding(ActionName, bIsSecondary);
		BuildKeyBindingsList();
		BP_RefreshBindingsList();
	}
}

void UShooterKeyBindingsUI::ConfirmKeyConflict()
{
	if (!PendingConflictKey.IsValid() || ActionBeingRebound == NAME_None)
	{
		return;
	}

	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		// Clear the conflicting binding
		Settings->ClearKeyBinding(ConflictingActionName, false);
		Settings->ClearKeyBinding(ConflictingActionName, true);

		// Set the new binding
		Settings->SetKeyBinding(ActionBeingRebound, PendingConflictKey, bIsRebindingSecondary);

		// Update UI
		BuildKeyBindingsList();
		BP_OnKeyBindingChanged(ActionBeingRebound, PendingConflictKey, bIsRebindingSecondary);
		BP_RefreshBindingsList();
	}

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
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->ResetKeyBindingsToDefaults();
		BuildKeyBindingsList();
		BP_RefreshBindingsList();
	}
}

void UShooterKeyBindingsUI::ResetBindingToDefault(FName ActionName)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		// Clear the custom binding - next time it will use default
		Settings->ClearKeyBinding(ActionName, false);
		Settings->ClearKeyBinding(ActionName, true);

		// Re-initialize to get the default
		Settings->InitializeDefaultKeyBindings();

		BuildKeyBindingsList();
		BP_RefreshBindingsList();
	}
}

void UShooterKeyBindingsUI::ApplyKeyBindings()
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->ApplyKeyBindings();
		Settings->SaveSettings();
	}
}

void UShooterKeyBindingsUI::CloseMenu()
{
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
		EKeys::Console
	};

	return Key.IsValid() && !ReservedKeys.Contains(Key);
}

// ==================== Protected Methods ====================

UShooterGameSettings* UShooterKeyBindingsUI::GetGameSettings() const
{
	return UShooterGameSettings::GetShooterGameSettings();
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

	UShooterGameSettings* Settings = GetGameSettings();
	if (!Settings)
	{
		CancelKeyListening();
		return;
	}

	// Check for conflicts
	FName ConflictAction;
	if (Settings->IsKeyAlreadyBound(PressedKey, ConflictAction))
	{
		// Don't conflict with self
		if (ConflictAction != ActionBeingRebound)
		{
			// Store pending conflict and notify Blueprint
			PendingConflictKey = PressedKey;
			ConflictingActionName = ConflictAction;

			BP_OnKeyConflict(PressedKey, ActionBeingRebound, ConflictAction);
			return;
		}
	}

	// No conflict, apply the binding
	Settings->SetKeyBinding(ActionBeingRebound, PressedKey, bIsRebindingSecondary);

	// Update UI
	BuildKeyBindingsList();
	BP_OnKeyBindingChanged(ActionBeingRebound, PressedKey, bIsRebindingSecondary);
	BP_RefreshBindingsList();

	// Reset listening state
	CancelKeyListening();
}

void UShooterKeyBindingsUI::BuildKeyBindingsList()
{
	CachedBindings.Empty();

	UShooterGameSettings* Settings = GetGameSettings();
	if (!Settings)
	{
		return;
	}

	// Build from the static display info map
	for (const auto& Pair : ActionDisplayInfoMap)
	{
		FKeyBindingDisplayInfo Info = Pair.Value;

		// Get current key bindings from settings
		Info.PrimaryKey = Settings->GetKeyForAction(Pair.Key, false);
		Info.SecondaryKey = Settings->GetKeyForAction(Pair.Key, true);

		CachedBindings.Add(Info);
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
