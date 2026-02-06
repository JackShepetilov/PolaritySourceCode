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

	/** ADS (Aim Down Sights) sensitivity multiplier */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Controls", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float ADSSensitivityMultiplier;

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
