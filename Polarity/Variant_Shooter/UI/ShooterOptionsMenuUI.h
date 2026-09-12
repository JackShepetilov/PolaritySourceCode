// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterOptionsMenuUI.generated.h"

class UShooterGameSettings;
class UShooterSettingsSubsystem;
class UShooterKeyBindingsUI;
class USlider;
class USpinBox;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnOptionsMenuClosed);

/**
 * Settings category for tab navigation.
 */
UENUM(BlueprintType)
enum class ESettingsCategory : uint8
{
	Audio,
	Controls,
	Graphics,
	Gameplay,
	Accessibility,
	KeyBindings
};

/**
 * Options Menu UI widget for the shooter game.
 * Provides tabs for different settings categories.
 *
 * Blueprint should:
 * - Create UI for each category (sliders, checkboxes, dropdowns)
 * - Bind to the C++ methods to get/set values
 * - Use BP_OnCategoryChanged to switch visible panels
 */
UCLASS(abstract)
class POLARITY_API UShooterOptionsMenuUI : public UUserWidget
{
	GENERATED_BODY()

public:

	// ==================== Delegates ====================

	/** Broadcast when options menu is closed (Back button pressed) */
	UPROPERTY(BlueprintAssignable, Category = "Settings")
	FOnOptionsMenuClosed OnOptionsMenuClosed;

	// ==================== Initialization ====================

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ==================== Blueprint Events ====================

	/** Called when the menu is opened */
	UFUNCTION(BlueprintImplementableEvent, Category = "Settings|Events", meta = (DisplayName = "OnMenuOpened"))
	void BP_OnMenuOpened();

	/** Called when the menu is closed */
	UFUNCTION(BlueprintImplementableEvent, Category = "Settings|Events", meta = (DisplayName = "OnMenuClosed"))
	void BP_OnMenuClosed();

	/** Called when category tab changes */
	UFUNCTION(BlueprintImplementableEvent, Category = "Settings|Events", meta = (DisplayName = "OnCategoryChanged"))
	void BP_OnCategoryChanged(ESettingsCategory NewCategory);

	/** Called when any setting is modified (before apply) */
	UFUNCTION(BlueprintImplementableEvent, Category = "Settings|Events", meta = (DisplayName = "OnSettingModified"))
	void BP_OnSettingModified(FName SettingName);

	/** Called when settings are applied */
	UFUNCTION(BlueprintImplementableEvent, Category = "Settings|Events", meta = (DisplayName = "OnSettingsApplied"))
	void BP_OnSettingsApplied();

	/** Called when settings are reverted */
	UFUNCTION(BlueprintImplementableEvent, Category = "Settings|Events", meta = (DisplayName = "OnSettingsReverted"))
	void BP_OnSettingsReverted();

	/** Called to refresh all UI elements with current values */
	UFUNCTION(BlueprintImplementableEvent, Category = "Settings|Events", meta = (DisplayName = "RefreshAllUI"))
	void BP_RefreshAllUI();

	// ==================== Navigation ====================

	/** Switch to a specific settings category */
	UFUNCTION(BlueprintCallable, Category = "Settings|Navigation")
	void SwitchCategory(ESettingsCategory NewCategory);

	/** Get the current category */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Navigation")
	ESettingsCategory GetCurrentCategory() const { return CurrentCategory; }

	/** Close the options menu and return to pause menu */
	UFUNCTION(BlueprintCallable, Category = "Settings|Navigation")
	void CloseMenu();

	/** Open key bindings sub-menu */
	UFUNCTION(BlueprintCallable, Category = "Settings|Navigation")
	void OpenKeyBindings();

	// ==================== Settings Actions ====================

	/** Apply all pending changes */
	UFUNCTION(BlueprintCallable, Category = "Settings|Actions")
	void ApplySettings();

	/** Revert to last saved settings */
	UFUNCTION(BlueprintCallable, Category = "Settings|Actions")
	void RevertSettings();

	/** Reset current category to defaults */
	UFUNCTION(BlueprintCallable, Category = "Settings|Actions")
	void ResetCategoryToDefaults();

	/** Reset all settings to defaults */
	UFUNCTION(BlueprintCallable, Category = "Settings|Actions")
	void ResetAllToDefaults();

	/** Check if there are unsaved changes */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Actions")
	bool HasUnsavedChanges() const { return bHasUnsavedChanges; }

	// ==================== Audio Settings ====================

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	float GetMasterVolume() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetMasterVolume(float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	float GetMusicVolume() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetMusicVolume(float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	float GetSFXVolume() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetSFXVolume(float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	float GetVoiceVolume() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetVoiceVolume(float Value);

	// ==================== Controls Settings ====================

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	float GetMouseSensitivity() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetMouseSensitivity(float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	float GetMouseSensitivityX() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetMouseSensitivityX(float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	float GetMouseSensitivityY() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetMouseSensitivityY(float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	float GetADSSensitivityMultiplier() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetADSSensitivityMultiplier(float Value);

	/** Mouse DPI. Feeds the cm/360 and eDPI readout only, never the turn. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	int32 GetMouseDpi() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetMouseDpi(int32 Value);

	/** Centimetres of mousepad for one full 360, for the live readout under the slider. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Controls")
	float GetCentimetersPer360() const;

	/** eDPI: sensitivity times DPI, the number pro configs get compared by. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Controls")
	float GetEffectiveDpi() const;

	/** The whole readout as one line, ready to bind to a TextBlock. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Controls")
	FText GetSensitivityReadout() const;

	// ==================== The sensitivity row ====================
	//
	// The widgets are found by name and wired here, so the row's behaviour lives in one place
	// instead of in loose Blueprint nodes. All four are optional: a menu missing any of them
	// still opens, and whatever is present still works.

	/** Drag the sensitivity. Range is set from code to the Apex scale, not in the asset. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USlider> Slider_MouseSensitivity;

	/** Type the sensitivity. This is the one that lets an Apex config be copied digit for digit. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USpinBox> SpinBox_LookSensitivity;

	/** Type the mouse DPI, so the line below can say a true cm/360. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USpinBox> SpinBox_MouseDpi;

	/** Live readout under the row: cm/360, eDPI, degrees per count. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_SensitivityReadout;

	/** Push the current settings back into the row's widgets. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void RefreshSensitivityWidgets();

protected:

	/** Set the widgets' ranges and hook their events. Runs once, on construct. */
	void SetupSensitivityWidgets();

	UFUNCTION()
	void HandleSensitivitySliderChanged(float Value);

	UFUNCTION()
	void HandleSensitivitySpinBoxChanged(float Value);

	UFUNCTION()
	void HandleMouseDpiSpinBoxChanged(float Value);

	/** Guards the slider and the spin box against answering each other's updates forever. */
	bool bUpdatingSensitivityWidgets = false;

public:

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	bool GetInvertMouseY() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetInvertMouseY(bool bValue);

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	bool GetInvertMouseX() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetInvertMouseX(bool bValue);

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	bool GetToggleADS() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetToggleADS(bool bValue);

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	bool GetToggleCrouch() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetToggleCrouch(bool bValue);

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	bool GetToggleSprint() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetToggleSprint(bool bValue);

	// ==================== Gameplay Settings ====================

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	float GetFieldOfView() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	void SetFieldOfView(float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	bool GetShowDamageNumbers() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	void SetShowDamageNumbers(bool bValue);

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	bool GetShowHitMarkers() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	void SetShowHitMarkers(bool bValue);

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	float GetScreenShakeIntensity() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	void SetScreenShakeIntensity(float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	int32 GetCrosshairType() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	void SetCrosshairType(int32 Value);

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	FLinearColor GetCrosshairColor() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	void SetCrosshairColor(FLinearColor Value);

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	float GetCrosshairSize() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	void SetCrosshairSize(float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	bool GetShowSpeedometer() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	void SetShowSpeedometer(bool bValue);

	// ==================== Graphics Settings (delegates to UGameUserSettings) ====================

	/** Get available screen resolutions */
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	TArray<FIntPoint> GetAvailableResolutions() const;

	/** Get current resolution */
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	FIntPoint GetCurrentResolution() const;

	/** Set screen resolution */
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	void SetResolution(FIntPoint NewResolution);

	/** Get fullscreen mode (0=Fullscreen, 1=WindowedFullscreen, 2=Windowed) */
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	int32 GetFullscreenMode() const;

	/** Set fullscreen mode */
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	void SetFullscreenMode(int32 Mode);

	/** Get VSync enabled */
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	bool GetVSyncEnabled() const;

	/** Set VSync */
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	void SetVSyncEnabled(bool bEnabled);

	/** Get frame rate limit (0 = unlimited) */
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	int32 GetFrameRateLimit() const;

	/** Set frame rate limit */
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	void SetFrameRateLimit(int32 Limit);

	/** Get overall quality preset (0=Low, 1=Medium, 2=High, 3=Epic, 4=Cinematic) */
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	int32 GetOverallQuality() const;

	/** Set overall quality preset */
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	void SetOverallQuality(int32 Quality);

	// ==================== Accessibility Settings ====================

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	int32 GetColorblindMode() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	void SetColorblindMode(int32 Mode);

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	float GetColorblindIntensity() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	void SetColorblindIntensity(float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	bool GetSubtitlesEnabled() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	void SetSubtitlesEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	float GetSubtitleSize() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	void SetSubtitleSize(float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	bool GetHighContrastUI() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	void SetHighContrastUI(bool bEnabled);

protected:

	/** Current active category */
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	ESettingsCategory CurrentCategory;

	/** Track if there are unsaved changes */
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	bool bHasUnsavedChanges;

	/** Key bindings widget class to spawn */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings|UI")
	TSubclassOf<UShooterKeyBindingsUI> KeyBindingsWidgetClass;

	/** Spawned key bindings widget */
	UPROPERTY()
	TObjectPtr<UShooterKeyBindingsUI> KeyBindingsWidget;

	/** Get game settings */
	UShooterGameSettings* GetGameSettings() const;

	/** Get settings subsystem */
	UShooterSettingsSubsystem* GetSettingsSubsystem() const;

	/** Mark that a setting has been modified */
	void MarkSettingModified(FName SettingName);

private:

	/** Called when key bindings menu closes itself */
	UFUNCTION()
	void OnKeyBindingsMenuClosedHandler();
};
