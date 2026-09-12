// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/World.h"
#include "ShooterSettingsSubsystem.generated.h"

class UShooterGameSettings;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSettingsChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAudioSettingsChanged, float, MasterVolume);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSensitivityChanged, float, NewSensitivity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFOVChanged, float, NewFOV);

/**
 * Game Instance Subsystem for managing game settings.
 * Provides global access to ShooterGameSettings and broadcasts setting changes.
 *
 * Access via: UGameplayStatics::GetGameInstance()->GetSubsystem<UShooterSettingsSubsystem>()
 * Or in Blueprint: Get Game Instance -> Get Subsystem (ShooterSettingsSubsystem)
 */
UCLASS()
class POLARITY_API UShooterSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	// ==================== Lifecycle ====================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ==================== Delegates ====================

	/** Broadcast when any setting changes */
	UPROPERTY(BlueprintAssignable, Category = "Settings")
	FOnSettingsChanged OnSettingsChanged;

	/** Broadcast when audio settings change */
	UPROPERTY(BlueprintAssignable, Category = "Settings|Audio")
	FOnAudioSettingsChanged OnAudioSettingsChanged;

	/** Broadcast when mouse sensitivity changes */
	UPROPERTY(BlueprintAssignable, Category = "Settings|Controls")
	FOnSensitivityChanged OnSensitivityChanged;

	/** Broadcast when FOV changes */
	UPROPERTY(BlueprintAssignable, Category = "Settings|Gameplay")
	FOnFOVChanged OnFOVChanged;

	// ==================== Settings Access ====================

	/** Get the game settings object */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings")
	UShooterGameSettings* GetSettings() const;

	// ==================== Quick Access Methods ====================

	/** Get the look sensitivity. Kept name; it now reads the one Apex-scale number. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Controls")
	float GetMouseSensitivity() const;

	/** Set the look sensitivity. Kept name; forwards to the one Apex-scale number. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetMouseSensitivity(float NewSensitivity);

	/** Look sensitivity on the Apex scale: 1.0 turns 0.022 degrees per mouse count. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Controls")
	float GetLookSensitivity() const;

	/** Set the look sensitivity and apply it to the view straight away. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetLookSensitivity(float NewSensitivity);

	/** The DPI the player told us about, used only for the readout. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Controls")
	int32 GetMouseDpi() const;

	/** Tell the game the mouse DPI so cm/360 and eDPI mean something. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetMouseDpi(int32 NewDpi);

	/** Centimetres of mousepad for one full 360 at the current sensitivity and DPI. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Controls")
	float GetCentimetersPer360() const;

	/** eDPI: sensitivity times DPI, the number pro configs get compared by. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Controls")
	float GetEffectiveDpi() const;

	/** One ready-made line for the menu, with all of the above in it. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Controls")
	FText GetSensitivityReadout() const;

	/** Get current field of view */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Gameplay")
	float GetFieldOfView() const;

	/** Set field of view and broadcast change */
	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	void SetFieldOfView(float NewFOV);

	/** Get screen shake intensity */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Gameplay")
	float GetScreenShakeIntensity() const;

	/** Set screen shake intensity */
	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	void SetScreenShakeIntensity(float NewIntensity);

	/** Get master volume */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Audio")
	float GetMasterVolume() const;

	/** Set master volume and broadcast change */
	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetMasterVolume(float NewVolume);

	/** Check if damage numbers are enabled */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Gameplay")
	bool AreDamageNumbersEnabled() const;

	/** Check if Y axis is inverted */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Controls")
	bool IsMouseYInverted() const;

	// ==================== Settings Management ====================

	/** Save all settings to config file */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SaveSettings();

	/** Load settings from config file */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void LoadSettings();

	/** Apply all settings to the game */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ApplyAllSettings();

	/** Reset all settings to defaults */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ResetAllToDefaults();

	/** Notify that settings have changed (broadcasts delegates) */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void NotifySettingsChanged();

private:

	/** Called after a map load, once the new world is ready to receive per-world settings. */
	void OnPostLoadMap(UWorld* LoadedWorld);

	/** Handle for the completed map-load delegate. */
	FDelegateHandle PostLoadMapHandle;

	/** Cached pointer to game settings */
	UPROPERTY()
	TObjectPtr<UShooterGameSettings> CachedSettings;
};
