// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "ShooterGameSettings.generated.h"

/**
 * Custom key binding entry for remappable actions.
 */
USTRUCT(BlueprintType)
struct FKeyBindingEntry
{
	GENERATED_BODY()

	/** The input action name (matches Enhanced Input Action name) */
	UPROPERTY(Config, BlueprintReadWrite, Category = "KeyBinding")
	FName ActionName;

	/** Primary key binding */
	UPROPERTY(Config, BlueprintReadWrite, Category = "KeyBinding")
	FKey PrimaryKey;

	/** Secondary key binding (optional) */
	UPROPERTY(Config, BlueprintReadWrite, Category = "KeyBinding")
	FKey SecondaryKey;

	FKeyBindingEntry()
		: ActionName(NAME_None)
		, PrimaryKey(EKeys::Invalid)
		, SecondaryKey(EKeys::Invalid)
	{}

	FKeyBindingEntry(FName InActionName, FKey InPrimaryKey, FKey InSecondaryKey = EKeys::Invalid)
		: ActionName(InActionName)
		, PrimaryKey(InPrimaryKey)
		, SecondaryKey(InSecondaryKey)
	{}
};

/**
 * Custom Game User Settings for Polarity Shooter.
 * Extends UGameUserSettings to add game-specific options.
 *
 * Automatically saved to GameUserSettings.ini
 * Register in DefaultEngine.ini: GameUserSettingsClassName=/Script/Polarity.ShooterGameSettings
 */
UCLASS()
class POLARITY_API UShooterGameSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:

	UShooterGameSettings();

	// ==================== Static Access ====================

	/** Get the game settings singleton (creates if needed) */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	static UShooterGameSettings* GetShooterGameSettings();

	// ==================== Audio Settings ====================

	/** Master volume (0.0 - 1.0) */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MasterVolume;

	/** Music volume (0.0 - 1.0) */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MusicVolume;

	/** Sound effects volume (0.0 - 1.0) */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SFXVolume;

	/** Voice/dialogue volume (0.0 - 1.0) */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VoiceVolume;

	// ==================== Audio Assets (Assign in Blueprint/DefaultGame.ini) ====================

	/** Sound Mix to use for volume adjustments */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Assets")
	TSoftObjectPtr<class USoundMix> AudioSoundMix;

	/** Sound Class for Music */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Assets")
	TSoftObjectPtr<class USoundClass> MusicSoundClass;

	/** Sound Class for Sound Effects */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Assets")
	TSoftObjectPtr<class USoundClass> SFXSoundClass;

	/** Sound Class for Voice/Dialogue */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Assets")
	TSoftObjectPtr<class USoundClass> VoiceSoundClass;

	// ==================== Controls Settings ====================

	/** Mouse sensitivity multiplier */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Controls", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float MouseSensitivity;

	/** Mouse sensitivity for X axis (horizontal) */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Controls", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float MouseSensitivityX;

	/** Mouse sensitivity for Y axis (vertical) */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Controls", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float MouseSensitivityY;

	/** ADS (Aim Down Sights) sensitivity multiplier. Apex meaning: 1.0 is "same as the hip". */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Controls", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float ADSSensitivityMultiplier;

	// ==================== Look sensitivity, one scale ====================
	//
	// Everything the player turns by goes through a single number on Apex's own scale:
	//     degrees per mouse count = ApexYawPerCount * LookSensitivity
	// where ApexYawPerCount is 0.022, the m_yaw of the Source/Titanfall line Apex sits on.
	// So a number typed here means in this game exactly what the same number means there.

	/** Look sensitivity on the Apex scale. 1.0 turns 0.022 degrees per mouse count. */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Controls", meta = (ClampMin = "0.05", ClampMax = "20.0"))
	float LookSensitivity;

	/** Mouse DPI. Only feeds the cm/360 and eDPI readout, never the turn itself. */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Controls", meta = (ClampMin = "100", ClampMax = "32000"))
	int32 MouseDpi;

	/** How many Enhanced Input units arrive per mouse count. 1.0 by the engine's own contract
	 *  (FSceneViewport hands the pixel/count delta over untouched). Only calibration moves it. */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Controls|Advanced", meta = (ClampMin = "0.001", ClampMax = "100.0"))
	float LookUnitsPerCount;

	/** The one-time carry of the old MouseSensitivity onto the Apex scale has already run. */
	UPROPERTY(Config)
	bool bLookSensitivityMigrated;

	/** Apex's m_yaw: degrees turned per mouse count at sensitivity 1.0. */
	static constexpr float ApexYawPerCount = 0.022f;

	/** Invert Y axis for mouse look */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Controls")
	bool bInvertMouseY;

	/** Invert X axis for mouse look */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Controls")
	bool bInvertMouseX;

	/** Toggle or hold for ADS */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Controls")
	bool bToggleADS;

	/** Toggle or hold for crouch */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Controls")
	bool bToggleCrouch;

	/** Toggle or hold for sprint */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Controls")
	bool bToggleSprint;

	// ==================== Gameplay Settings ====================

	/** Field of View (degrees) */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Gameplay", meta = (ClampMin = "60.0", ClampMax = "120.0"))
	float FieldOfView;

	/** Show floating damage numbers */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Gameplay")
	bool bShowDamageNumbers;

	/** Show hit markers */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Gameplay")
	bool bShowHitMarkers;

	/** Screen shake intensity (0.0 = off, 1.0 = normal) */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Gameplay", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ScreenShakeIntensity;

	/** Crosshair type (0 = default, 1 = dot, 2 = cross, etc.) */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Gameplay")
	int32 CrosshairType;

	/** Crosshair color */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Gameplay")
	FLinearColor CrosshairColor;

	/** Crosshair size multiplier */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Gameplay", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float CrosshairSize;

	/** Show speedometer UI */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Gameplay")
	bool bShowSpeedometer;

	// ==================== Accessibility Settings ====================

	/** Colorblind mode (0 = off, 1 = protanopia, 2 = deuteranopia, 3 = tritanopia) */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Accessibility")
	int32 ColorblindMode;

	/** Colorblind severity/intensity */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Accessibility", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ColorblindIntensity;

	/** Enable subtitles */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Accessibility")
	bool bEnableSubtitles;

	/** Subtitle text size multiplier */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Accessibility", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float SubtitleSize;

	/** High contrast mode for UI */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Accessibility")
	bool bHighContrastUI;

	// ==================== Key Bindings ====================

	/** Custom key bindings - stored as array of action->key mappings */
	UPROPERTY(Config, BlueprintReadWrite, Category = "KeyBindings")
	TArray<FKeyBindingEntry> CustomKeyBindings;

	// ==================== Methods ====================

	/** Apply audio settings to the audio system */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ApplyAudioSettings();

	/** Apply gameplay settings (FOV, etc.) */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ApplyGameplaySettings();

	/** Apply control settings (mouse sensitivity, etc.) */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ApplyControlSettings();

	/** Apply all custom settings */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ApplyAllCustomSettings();

	// ==================== Look sensitivity readout ====================

	/** Degrees the view turns for one mouse count, at the current sensitivity. */
	UFUNCTION(BlueprintPure, Category = "Settings|Look")
	float GetDegreesPerCount() const;

	/** Centimetres of mousepad for a full 360, at the DPI written in the settings. */
	UFUNCTION(BlueprintPure, Category = "Settings|Look")
	float GetCentimetersPer360() const;

	/** Inches of mousepad for a full 360, for configs quoted the American way. */
	UFUNCTION(BlueprintPure, Category = "Settings|Look")
	float GetInchesPer360() const;

	/** eDPI: sensitivity times DPI, the number pro configs are compared by. */
	UFUNCTION(BlueprintPure, Category = "Settings|Look")
	float GetEffectiveDpi() const;

	/** One line for the menu: "1.60   |   32.4 cm/360   |   eDPI 1280". */
	UFUNCTION(BlueprintPure, Category = "Settings|Look")
	FText GetSensitivityReadout() const;

	/** Trust a measured 360 over the engine, and fix LookUnitsPerCount so the readout is true. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Look")
	void CalibrateFromMeasured360(float MeasuredCentimeters);

	/** Carry the old MouseSensitivity onto the Apex scale once, keeping the feel it had. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Look")
	void MigrateLegacySensitivity();

	/** Reset all custom settings to defaults */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ResetToDefaults();

	/** Reset only audio settings to defaults */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ResetAudioToDefaults();

	/** Reset only control settings to defaults */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ResetControlsToDefaults();

	/** Reset only gameplay settings to defaults */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ResetGameplayToDefaults();

	/** Reset only key bindings to defaults */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ResetKeyBindingsToDefaults();

	// ==================== Key Binding Methods ====================

	/** Get the key bound to an action */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	FKey GetKeyForAction(FName ActionName, bool bSecondary = false) const;

	/** Set a key binding for an action */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	void SetKeyBinding(FName ActionName, FKey NewKey, bool bSecondary = false);

	/** Check if a key is already bound to another action */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	bool IsKeyAlreadyBound(FKey Key, FName& OutConflictingAction) const;

	/** Clear a key binding */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	void ClearKeyBinding(FName ActionName, bool bSecondary = false);

	/** Apply key bindings to Enhanced Input system */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	void ApplyKeyBindings();

	/** Initialize default key bindings from current Enhanced Input mappings */
	UFUNCTION(BlueprintCallable, Category = "KeyBindings")
	void InitializeDefaultKeyBindings();

protected:

	/** Set default values for custom settings */
	void SetCustomDefaults();

	/** Find or create a key binding entry for an action */
	FKeyBindingEntry* FindOrCreateKeyBinding(FName ActionName);

	/** Find a key binding entry (const version) */
	const FKeyBindingEntry* FindKeyBinding(FName ActionName) const;
};
