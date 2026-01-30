// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "ShooterKeyBindingsUI.generated.h"

class UShooterGameSettings;

/**
 * Display information for a single key binding row.
 */
USTRUCT(BlueprintType)
struct FKeyBindingDisplayInfo
{
	GENERATED_BODY()

	/** Internal action name */
	UPROPERTY(BlueprintReadOnly, Category = "KeyBinding")
	FName ActionName;

	/** Localized display name for the action */
	UPROPERTY(BlueprintReadOnly, Category = "KeyBinding")
	FText DisplayName;

	/** Category for grouping (Movement, Combat, etc.) */
	UPROPERTY(BlueprintReadOnly, Category = "KeyBinding")
	FText Category;

	/** Primary key currently bound */
	UPROPERTY(BlueprintReadOnly, Category = "KeyBinding")
	FKey PrimaryKey;

	/** Secondary key currently bound */
	UPROPERTY(BlueprintReadOnly, Category = "KeyBinding")
	FKey SecondaryKey;

	/** Whether this binding can be remapped */
	UPROPERTY(BlueprintReadOnly, Category = "KeyBinding")
	bool bCanRemap;

	FKeyBindingDisplayInfo()
		: ActionName(NAME_None)
		, DisplayName(FText::GetEmpty())
		, Category(FText::GetEmpty())
		, PrimaryKey(EKeys::Invalid)
		, SecondaryKey(EKeys::Invalid)
		, bCanRemap(true)
	{}
};

/**
 * Key Bindings UI widget for remapping controls.
 *
 * Blueprint should:
 * - Display a list of key bindings using GetAllKeyBindings()
 * - Handle the key listening mode when BP_StartKeyListening is called
 * - Show conflict dialogs when BP_OnKeyConflict is called
 */
UCLASS(abstract)
class POLARITY_API UShooterKeyBindingsUI : public UUserWidget
{
	GENERATED_BODY()

public:

	// ==================== Initialization ====================

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	// ==================== Blueprint Events ====================

	/** Called when the key bindings UI is opened */
	UFUNCTION(BlueprintImplementableEvent, Category = "KeyBindings|Events", meta = (DisplayName = "OnKeyBindingsOpened"))
	void BP_OnKeyBindingsOpened();

	/** Called when the key bindings UI is closed */
	UFUNCTION(BlueprintImplementableEvent, Category = "KeyBindings|Events", meta = (DisplayName = "OnKeyBindingsClosed"))
	void BP_OnKeyBindingsClosed();

	/** Called when starting to listen for a new key press */
	UFUNCTION(BlueprintImplementableEvent, Category = "KeyBindings|Events", meta = (DisplayName = "StartKeyListening"))
	void BP_StartKeyListening(FName ActionName, bool bIsSecondary);

	/** Called when key listening is cancelled */
	UFUNCTION(BlueprintImplementableEvent, Category = "KeyBindings|Events", meta = (DisplayName = "StopKeyListening"))
	void BP_StopKeyListening();

	/** Called when a key binding is successfully changed */
	UFUNCTION(BlueprintImplementableEvent, Category = "KeyBindings|Events", meta = (DisplayName = "OnKeyBindingChanged"))
	void BP_OnKeyBindingChanged(FName ActionName, FKey NewKey, bool bIsSecondary);

	/** Called when there's a key conflict - Blueprint should show confirmation dialog */
	UFUNCTION(BlueprintImplementableEvent, Category = "KeyBindings|Events", meta = (DisplayName = "OnKeyConflict"))
	void BP_OnKeyConflict(FKey ConflictingKey, FName CurrentAction, FName ConflictingAction);

	/** Called to refresh the binding list display */
	UFUNCTION(BlueprintImplementableEvent, Category = "KeyBindings|Events", meta = (DisplayName = "RefreshBindingsList"))
	void BP_RefreshBindingsList();

	// ==================== Key Binding Data ====================

	/** Get all key bindings for display */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	TArray<FKeyBindingDisplayInfo> GetAllKeyBindings() const;

	/** Get bindings for a specific category */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	TArray<FKeyBindingDisplayInfo> GetBindingsForCategory(const FText& Category) const;

	/** Get all available categories */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	TArray<FText> GetAllCategories() const;

	// ==================== Key Binding Actions ====================

	/** Start listening for a key press to bind to an action */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	void StartListeningForKey(FName ActionName, bool bIsSecondary = false);

	/** Cancel key listening mode */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	void CancelKeyListening();

	/** Check if currently listening for a key */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "KeyBindings")
	bool IsListeningForKey() const { return bIsListeningForKey; }

	/** Get the action currently being rebound */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "KeyBindings")
	FName GetActionBeingRebound() const { return ActionBeingRebound; }

	/** Clear a specific key binding */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	void ClearBinding(FName ActionName, bool bIsSecondary = false);

	/** Confirm overwriting a conflicting key binding */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	void ConfirmKeyConflict();

	/** Cancel overwriting a conflicting key binding */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	void CancelKeyConflict();

	/** Reset all key bindings to defaults */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	void ResetAllToDefaults();

	/** Reset a specific action's binding to default */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	void ResetBindingToDefault(FName ActionName);

	/** Apply and save all key binding changes */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	void ApplyKeyBindings();

	/** Close the key bindings menu */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	void CloseMenu();

	// ==================== Utility ====================

	/** Get the display name for a key */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "KeyBindings")
	static FText GetKeyDisplayName(FKey Key);

	/** Check if a key is valid for binding (not reserved) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "KeyBindings")
	static bool IsKeyValidForBinding(FKey Key);

protected:

	/** Whether we're currently listening for a key press */
	UPROPERTY(BlueprintReadOnly, Category = "KeyBindings")
	bool bIsListeningForKey;

	/** The action we're rebinding */
	UPROPERTY(BlueprintReadOnly, Category = "KeyBindings")
	FName ActionBeingRebound;

	/** Whether we're rebinding the secondary slot */
	UPROPERTY(BlueprintReadOnly, Category = "KeyBindings")
	bool bIsRebindingSecondary;

	/** Pending key that has a conflict */
	UPROPERTY()
	FKey PendingConflictKey;

	/** Action that conflicts with the pending key */
	UPROPERTY()
	FName ConflictingActionName;

	/** Get game settings */
	UShooterGameSettings* GetGameSettings() const;

	/** Process a key press during listening mode */
	void ProcessKeyPress(FKey PressedKey);

	/** Build the list of key binding display info */
	void BuildKeyBindingsList();

	/** Cached list of key bindings for display */
	UPROPERTY()
	TArray<FKeyBindingDisplayInfo> CachedBindings;

	/** Static mapping of action names to display info */
	static TMap<FName, FKeyBindingDisplayInfo> ActionDisplayInfoMap;

	/** Initialize the action display info map */
	static void InitializeActionDisplayInfoMap();
};
