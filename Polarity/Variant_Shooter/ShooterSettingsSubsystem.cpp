// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterSettingsSubsystem.h"
#include "ShooterGameSettings.h"

void UShooterSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Cache the settings pointer
	CachedSettings = UShooterGameSettings::GetShooterGameSettings();

	// Load settings on startup
	LoadSettings();

	UE_LOG(LogTemp, Log, TEXT("ShooterSettingsSubsystem initialized"));
}

void UShooterSettingsSubsystem::Deinitialize()
{
	// Save settings on shutdown
	SaveSettings();

	CachedSettings = nullptr;

	Super::Deinitialize();
}

UShooterGameSettings* UShooterSettingsSubsystem::GetSettings() const
{
	if (!CachedSettings)
	{
		// Try to get settings again if not cached
		return UShooterGameSettings::GetShooterGameSettings();
	}
	return CachedSettings;
}

// ==================== Quick Access Methods ====================

float UShooterSettingsSubsystem::GetMouseSensitivity() const
{
	if (UShooterGameSettings* Settings = GetSettings())
	{
		return Settings->MouseSensitivity;
	}
	return 1.0f;
}

void UShooterSettingsSubsystem::SetMouseSensitivity(float NewSensitivity)
{
	if (UShooterGameSettings* Settings = GetSettings())
	{
		Settings->MouseSensitivity = FMath::Clamp(NewSensitivity, 0.1f, 10.0f);
		OnSensitivityChanged.Broadcast(Settings->MouseSensitivity);
		OnSettingsChanged.Broadcast();
	}
}

float UShooterSettingsSubsystem::GetFieldOfView() const
{
	if (UShooterGameSettings* Settings = GetSettings())
	{
		return Settings->FieldOfView;
	}
	return 90.0f;
}

void UShooterSettingsSubsystem::SetFieldOfView(float NewFOV)
{
	if (UShooterGameSettings* Settings = GetSettings())
	{
		Settings->FieldOfView = FMath::Clamp(NewFOV, 60.0f, 120.0f);
		Settings->ApplyGameplaySettings();
		OnFOVChanged.Broadcast(Settings->FieldOfView);
		OnSettingsChanged.Broadcast();
	}
}

float UShooterSettingsSubsystem::GetScreenShakeIntensity() const
{
	if (UShooterGameSettings* Settings = GetSettings())
	{
		return Settings->ScreenShakeIntensity;
	}
	return 1.0f;
}

void UShooterSettingsSubsystem::SetScreenShakeIntensity(float NewIntensity)
{
	if (UShooterGameSettings* Settings = GetSettings())
	{
		Settings->ScreenShakeIntensity = FMath::Clamp(NewIntensity, 0.0f, 2.0f);
		OnSettingsChanged.Broadcast();
	}
}

float UShooterSettingsSubsystem::GetMasterVolume() const
{
	if (UShooterGameSettings* Settings = GetSettings())
	{
		return Settings->MasterVolume;
	}
	return 1.0f;
}

void UShooterSettingsSubsystem::SetMasterVolume(float NewVolume)
{
	if (UShooterGameSettings* Settings = GetSettings())
	{
		Settings->MasterVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
		Settings->ApplyAudioSettings();
		OnAudioSettingsChanged.Broadcast(Settings->MasterVolume);
		OnSettingsChanged.Broadcast();
	}
}

bool UShooterSettingsSubsystem::AreDamageNumbersEnabled() const
{
	if (UShooterGameSettings* Settings = GetSettings())
	{
		return Settings->bShowDamageNumbers;
	}
	return true;
}

bool UShooterSettingsSubsystem::IsMouseYInverted() const
{
	if (UShooterGameSettings* Settings = GetSettings())
	{
		return Settings->bInvertMouseY;
	}
	return false;
}

// ==================== Settings Management ====================

void UShooterSettingsSubsystem::SaveSettings()
{
	if (UShooterGameSettings* Settings = GetSettings())
	{
		Settings->SaveSettings();
		UE_LOG(LogTemp, Log, TEXT("Settings saved"));
	}
}

void UShooterSettingsSubsystem::LoadSettings()
{
	if (UShooterGameSettings* Settings = GetSettings())
	{
		Settings->LoadSettings();
		UE_LOG(LogTemp, Log, TEXT("Settings loaded"));
	}
}

void UShooterSettingsSubsystem::ApplyAllSettings()
{
	if (UShooterGameSettings* Settings = GetSettings())
	{
		Settings->ApplyAllCustomSettings();
		NotifySettingsChanged();
	}
}

void UShooterSettingsSubsystem::ResetAllToDefaults()
{
	if (UShooterGameSettings* Settings = GetSettings())
	{
		Settings->ResetToDefaults();
		NotifySettingsChanged();
	}
}

void UShooterSettingsSubsystem::NotifySettingsChanged()
{
	OnSettingsChanged.Broadcast();

	if (UShooterGameSettings* Settings = GetSettings())
	{
		OnAudioSettingsChanged.Broadcast(Settings->MasterVolume);
		OnSensitivityChanged.Broadcast(Settings->MouseSensitivity);
		OnFOVChanged.Broadcast(Settings->FieldOfView);
	}
}
