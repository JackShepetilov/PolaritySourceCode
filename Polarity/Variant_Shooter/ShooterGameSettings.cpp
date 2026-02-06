// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundClass.h"
#include "AudioDevice.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "UserSettings/EnhancedInputUserSettings.h"

UShooterGameSettings::UShooterGameSettings()
{
	SetCustomDefaults();
}

UShooterGameSettings* UShooterGameSettings::GetShooterGameSettings()
{
	return Cast<UShooterGameSettings>(UGameUserSettings::GetGameUserSettings());
}

void UShooterGameSettings::SetCustomDefaults()
{
	// Audio defaults
	MasterVolume = 1.0f;
	MusicVolume = 1.0f;
	SFXVolume = 1.0f;
	VoiceVolume = 1.0f;

	// Audio assets defaults
	AudioSoundMix = TSoftObjectPtr<USoundMix>(FSoftObjectPath(TEXT("/Game/Audio/Classes/NewSoundMix.NewSoundMix")));
	MusicSoundClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(TEXT("/Game/Audio/Classes/Music.Music")));
	SFXSoundClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(TEXT("/Game/Audio/Classes/SFX.SFX")));
	VoiceSoundClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(TEXT("/Game/Audio/Classes/Voice.Voice")));

	// Controls defaults
	MouseSensitivity = 1.0f;
	MouseSensitivityX = 1.0f;
	MouseSensitivityY = 1.0f;
	ADSSensitivityMultiplier = 0.7f;
	bInvertMouseY = false;
	bInvertMouseX = false;
	bToggleADS = false;
	bToggleCrouch = false;
	bToggleSprint = false;

	// Gameplay defaults
	FieldOfView = 90.0f;
	bShowDamageNumbers = true;
	bShowHitMarkers = true;
	ScreenShakeIntensity = 1.0f;
	CrosshairType = 0;
	CrosshairColor = FLinearColor::White;
	CrosshairSize = 1.0f;
	bShowSpeedometer = true;

	// Accessibility defaults
	ColorblindMode = 0;
	ColorblindIntensity = 1.0f;
	bEnableSubtitles = false;
	SubtitleSize = 1.0f;
	bHighContrastUI = false;
}

void UShooterGameSettings::ApplyAudioSettings()
{
	UE_LOG(LogTemp, Log, TEXT("[AudioDebug] === ApplyAudioSettings called ==="));
	UE_LOG(LogTemp, Log, TEXT("[AudioDebug] Values: Master=%.2f, Music=%.2f, SFX=%.2f, Voice=%.2f"),
		MasterVolume, MusicVolume, SFXVolume, VoiceVolume);

	// Log the asset paths being loaded
	UE_LOG(LogTemp, Log, TEXT("[AudioDebug] SoundMix path: %s"), *AudioSoundMix.ToString());
	UE_LOG(LogTemp, Log, TEXT("[AudioDebug] MusicClass path: %s"), *MusicSoundClass.ToString());
	UE_LOG(LogTemp, Log, TEXT("[AudioDebug] SFXClass path: %s"), *SFXSoundClass.ToString());
	UE_LOG(LogTemp, Log, TEXT("[AudioDebug] VoiceClass path: %s"), *VoiceSoundClass.ToString());

	// Load audio assets if they're set
	USoundMix* SoundMix = AudioSoundMix.LoadSynchronous();
	USoundClass* MusicClass = MusicSoundClass.LoadSynchronous();
	USoundClass* SFXClass = SFXSoundClass.LoadSynchronous();
	USoundClass* VoiceClass = VoiceSoundClass.LoadSynchronous();

	UE_LOG(LogTemp, Log, TEXT("[AudioDebug] Loaded: SoundMix=%s, Music=%s, SFX=%s, Voice=%s"),
		SoundMix ? TEXT("OK") : TEXT("NULL"),
		MusicClass ? TEXT("OK") : TEXT("NULL"),
		SFXClass ? TEXT("OK") : TEXT("NULL"),
		VoiceClass ? TEXT("OK") : TEXT("NULL"));

	if (!SoundMix)
	{
		UE_LOG(LogTemp, Error, TEXT("[AudioDebug] AudioSoundMix FAILED to load! Path: %s"), *AudioSoundMix.ToString());
		return;
	}

	// Get a world context for audio
	UWorld* World = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.WorldType == EWorldType::Game)
			{
				World = Context.World();
				break;
			}
		}
	}

	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[AudioDebug] No game world found!"));
		return;
	}

	// Push the sound mix if not already active
	UGameplayStatics::PushSoundMixModifier(World, SoundMix);
	UE_LOG(LogTemp, Log, TEXT("[AudioDebug] PushSoundMixModifier done"));

	// Apply volume for each Sound Class (multiplied by MasterVolume)
	if (MusicClass)
	{
		const float FinalMusicVolume = MusicVolume * MasterVolume;
		UGameplayStatics::SetSoundMixClassOverride(World, SoundMix, MusicClass, FinalMusicVolume, 1.0f, 0.0f, true);
		UE_LOG(LogTemp, Log, TEXT("[AudioDebug] Music applied: %.2f (%.2f * %.2f)"), FinalMusicVolume, MusicVolume, MasterVolume);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[AudioDebug] MusicSoundClass is NULL - skipping"));
	}

	if (SFXClass)
	{
		const float FinalSFXVolume = SFXVolume * MasterVolume;
		UGameplayStatics::SetSoundMixClassOverride(World, SoundMix, SFXClass, FinalSFXVolume, 1.0f, 0.0f, true);
		UE_LOG(LogTemp, Log, TEXT("[AudioDebug] SFX applied: %.2f (%.2f * %.2f)"), FinalSFXVolume, SFXVolume, MasterVolume);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[AudioDebug] SFXSoundClass is NULL - skipping"));
	}

	if (VoiceClass)
	{
		const float FinalVoiceVolume = VoiceVolume * MasterVolume;
		UGameplayStatics::SetSoundMixClassOverride(World, SoundMix, VoiceClass, FinalVoiceVolume, 1.0f, 0.0f, true);
		UE_LOG(LogTemp, Log, TEXT("[AudioDebug] Voice applied: %.2f (%.2f * %.2f)"), FinalVoiceVolume, VoiceVolume, MasterVolume);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[AudioDebug] VoiceSoundClass is NULL - skipping"));
	}

	UE_LOG(LogTemp, Log, TEXT("[AudioDebug] === ApplyAudioSettings done ==="));
}

void UShooterGameSettings::ApplyGameplaySettings()
{
	// FOV is typically applied per-camera or via PlayerCameraManager
	// ScreenShakeIntensity should be read by CameraShakeComponent

	// Get the first local player controller and apply FOV
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World())
			{
				if (APlayerController* PC = World->GetFirstPlayerController())
				{
					if (APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
					{
						CameraManager->SetFOV(FieldOfView);
					}
				}
			}
		}
	}
}

void UShooterGameSettings::ApplyControlSettings()
{
	// Apply mouse sensitivity to all player controllers
	// Note: In UE 5.0+ InputYawScale/InputPitchScale are deprecated
	// We use the deprecated setters which still work if bEnableLegacyInputScales is true in InputSettings
	// Alternatively, use Enhanced Input Scalar Modifier for modern approach
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World())
			{
				if (APlayerController* PC = World->GetFirstPlayerController())
				{
					// Default values are typically 2.5 for both
					// We multiply the base value by our sensitivity multipliers
					const float BaseSensitivity = 2.5f;

					const float YawScale = BaseSensitivity * MouseSensitivity * MouseSensitivityX * (bInvertMouseX ? -1.0f : 1.0f);
					const float PitchScale = BaseSensitivity * MouseSensitivity * MouseSensitivityY * (bInvertMouseY ? -1.0f : 1.0f);

					// Use deprecated setters (still work with bEnableLegacyInputScales=true)
					PRAGMA_DISABLE_DEPRECATION_WARNINGS
					PC->SetDeprecatedInputYawScale(YawScale);
					PC->SetDeprecatedInputPitchScale(PitchScale);
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
				}
			}
		}
	}
}

void UShooterGameSettings::ApplyAllCustomSettings()
{
	ApplyAudioSettings();
	ApplyGameplaySettings();
	ApplyControlSettings();
	ApplyKeyBindings();

	// Save to config file
	SaveSettings();
}

void UShooterGameSettings::ResetToDefaults()
{
	SetCustomDefaults();

	// Also reset parent class settings (graphics, resolution, etc.)
	SetToDefaults();
}

void UShooterGameSettings::ResetAudioToDefaults()
{
	MasterVolume = 1.0f;
	MusicVolume = 1.0f;
	SFXVolume = 1.0f;
	VoiceVolume = 1.0f;
}

void UShooterGameSettings::ResetControlsToDefaults()
{
	MouseSensitivity = 1.0f;
	MouseSensitivityX = 1.0f;
	MouseSensitivityY = 1.0f;
	ADSSensitivityMultiplier = 0.7f;
	bInvertMouseY = false;
	bInvertMouseX = false;
	bToggleADS = false;
	bToggleCrouch = false;
	bToggleSprint = false;
}

void UShooterGameSettings::ResetGameplayToDefaults()
{
	FieldOfView = 90.0f;
	bShowDamageNumbers = true;
	bShowHitMarkers = true;
	ScreenShakeIntensity = 1.0f;
	CrosshairType = 0;
	CrosshairColor = FLinearColor::White;
	CrosshairSize = 1.0f;
	bShowSpeedometer = true;
}

void UShooterGameSettings::ResetKeyBindingsToDefaults()
{
	CustomKeyBindings.Empty();
	InitializeDefaultKeyBindings();
}

// ==================== Key Binding Methods ====================

FKey UShooterGameSettings::GetKeyForAction(FName ActionName, bool bSecondary) const
{
	const FKeyBindingEntry* Entry = FindKeyBinding(ActionName);
	if (Entry)
	{
		return bSecondary ? Entry->SecondaryKey : Entry->PrimaryKey;
	}
	return EKeys::Invalid;
}

void UShooterGameSettings::SetKeyBinding(FName ActionName, FKey NewKey, bool bSecondary)
{
	FKeyBindingEntry* Entry = FindOrCreateKeyBinding(ActionName);
	if (Entry)
	{
		if (bSecondary)
		{
			Entry->SecondaryKey = NewKey;
		}
		else
		{
			Entry->PrimaryKey = NewKey;
		}
	}
}

bool UShooterGameSettings::IsKeyAlreadyBound(FKey Key, FName& OutConflictingAction) const
{
	if (!Key.IsValid())
	{
		return false;
	}

	for (const FKeyBindingEntry& Entry : CustomKeyBindings)
	{
		if (Entry.PrimaryKey == Key || Entry.SecondaryKey == Key)
		{
			OutConflictingAction = Entry.ActionName;
			return true;
		}
	}

	return false;
}

void UShooterGameSettings::ClearKeyBinding(FName ActionName, bool bSecondary)
{
	FKeyBindingEntry* Entry = FindOrCreateKeyBinding(ActionName);
	if (Entry)
	{
		if (bSecondary)
		{
			Entry->SecondaryKey = EKeys::Invalid;
		}
		else
		{
			Entry->PrimaryKey = EKeys::Invalid;
		}
	}
}

void UShooterGameSettings::ApplyKeyBindings()
{
	// In UE 5.3+, use EnhancedInputUserSettings for proper key remapping
	// This requires setting up PlayerMappableInputConfig in Project Settings

	// For now, key bindings are stored here and can be queried by the input system
	// The actual remapping happens through UEnhancedInputUserSettings

	// Get the local player's Enhanced Input User Settings
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World())
			{
				if (APlayerController* PC = World->GetFirstPlayerController())
				{
					if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
					{
						if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
						{
							if (UEnhancedInputUserSettings* UserSettings = InputSubsystem->GetUserSettings())
							{
								// Apply each custom key binding
								for (const FKeyBindingEntry& Binding : CustomKeyBindings)
								{
									// Use the Enhanced Input User Settings API to remap
									// This requires the Input Action to be marked as Player Mappable
									FMapPlayerKeyArgs Args;
									Args.MappingName = Binding.ActionName;
									Args.NewKey = Binding.PrimaryKey;
									Args.Slot = EPlayerMappableKeySlot::First;

									FGameplayTagContainer FailureReason;
									UserSettings->MapPlayerKey(Args, FailureReason);

									// Also map secondary key if valid
									if (Binding.SecondaryKey.IsValid())
									{
										Args.NewKey = Binding.SecondaryKey;
										Args.Slot = EPlayerMappableKeySlot::Second;
										UserSettings->MapPlayerKey(Args, FailureReason);
									}
								}

								// Save the user settings
								UserSettings->SaveSettings();
							}
						}
					}
				}
			}
		}
	}
}

void UShooterGameSettings::InitializeDefaultKeyBindings()
{
	// Initialize with common shooter bindings
	// These should match your Input Action names in Enhanced Input

	CustomKeyBindings.Empty();

	// Movement
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_Move"), EKeys::Invalid)); // WASD handled by axis
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_Jump"), EKeys::SpaceBar));
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_Crouch"), EKeys::LeftControl, EKeys::C));
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_Sprint"), EKeys::LeftShift));

	// Combat
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_Fire"), EKeys::LeftMouseButton));
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_ADS"), EKeys::RightMouseButton));
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_Reload"), EKeys::R));
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_Melee"), EKeys::V));

	// Abilities
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_Dash"), EKeys::LeftShift));
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_SwitchPolarity"), EKeys::Q));

	// Weapons
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_Weapon1"), EKeys::One));
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_Weapon2"), EKeys::Two));
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_Weapon3"), EKeys::Three));
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_NextWeapon"), EKeys::MouseScrollUp));
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_PrevWeapon"), EKeys::MouseScrollDown));

	// UI
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_Pause"), EKeys::Escape));
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_Scoreboard"), EKeys::Tab));
}

FKeyBindingEntry* UShooterGameSettings::FindOrCreateKeyBinding(FName ActionName)
{
	for (FKeyBindingEntry& Entry : CustomKeyBindings)
	{
		if (Entry.ActionName == ActionName)
		{
			return &Entry;
		}
	}

	// Not found, create new
	int32 Index = CustomKeyBindings.Add(FKeyBindingEntry(ActionName, EKeys::Invalid));
	return &CustomKeyBindings[Index];
}

const FKeyBindingEntry* UShooterGameSettings::FindKeyBinding(FName ActionName) const
{
	for (const FKeyBindingEntry& Entry : CustomKeyBindings)
	{
		if (Entry.ActionName == ActionName)
		{
			return &Entry;
		}
	}
	return nullptr;
}
