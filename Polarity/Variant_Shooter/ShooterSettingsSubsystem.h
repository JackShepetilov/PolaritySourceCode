// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
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

	/** Get current mouse sensitivity */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Settings|Controls")
	float GetMouseSensitivity() const;

	/** Set mouse sensitivity and broadcast change */
	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetMouseSensitivity(float NewSensitivity);

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

	/** Cached pointer to game settings */
	UPROPERTY()
	TObjectPtr<UShooterGameSettings> CachedSettings;
};
