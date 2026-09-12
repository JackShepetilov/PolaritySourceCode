// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameSettings.h"
#include "Coop/CoopPlayers.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundClass.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/IConsoleManager.h"
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
	ADSSensitivityMultiplier = 1.0f;
	LookSensitivity = 2.0f;
	MouseDpi = 800;
	LookUnitsPerCount = 1.0f;
	bLookSensitivityMigrated = false;
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

	// Load audio assets
	USoundMix* SoundMix = AudioSoundMix.LoadSynchronous();
	USoundClass* MusicClass = MusicSoundClass.LoadSynchronous();
	USoundClass* SFXClass = SFXSoundClass.LoadSynchronous();
	USoundClass* VoiceClass = VoiceSoundClass.LoadSynchronous();

	// Fallback: try LoadObject directly if soft refs failed
	if (!SoundMix)
	{
		SoundMix = LoadObject<USoundMix>(nullptr, TEXT("/Game/Audio/Classes/NewSoundMix.NewSoundMix"));
		UE_LOG(LogTemp, Warning, TEXT("[AudioDebug] SoundMix soft ref failed, fallback: %s"), SoundMix ? TEXT("OK") : TEXT("STILL NULL"));
	}
	if (!MusicClass)
	{
		MusicClass = LoadObject<USoundClass>(nullptr, TEXT("/Game/Audio/Classes/Music.Music"));
		UE_LOG(LogTemp, Warning, TEXT("[AudioDebug] MusicClass soft ref failed, fallback: %s"), MusicClass ? TEXT("OK") : TEXT("STILL NULL"));
	}
	if (!SFXClass)
	{
		SFXClass = LoadObject<USoundClass>(nullptr, TEXT("/Game/Audio/Classes/SFX.SFX"));
		UE_LOG(LogTemp, Warning, TEXT("[AudioDebug] SFXClass soft ref failed, fallback: %s"), SFXClass ? TEXT("OK") : TEXT("STILL NULL"));
	}
	if (!VoiceClass)
	{
		VoiceClass = LoadObject<USoundClass>(nullptr, TEXT("/Game/Audio/Classes/Voice.Voice"));
		UE_LOG(LogTemp, Warning, TEXT("[AudioDebug] VoiceClass soft ref failed, fallback: %s"), VoiceClass ? TEXT("OK") : TEXT("STILL NULL"));
	}

	UE_LOG(LogTemp, Log, TEXT("[AudioDebug] Loaded: SoundMix=%s, Music=%s, SFX=%s, Voice=%s"),
		SoundMix ? TEXT("OK") : TEXT("NULL"),
		MusicClass ? TEXT("OK") : TEXT("NULL"),
		SFXClass ? TEXT("OK") : TEXT("NULL"),
		VoiceClass ? TEXT("OK") : TEXT("NULL"));

	if (!SoundMix)
	{
		UE_LOG(LogTemp, Error, TEXT("[AudioDebug] SoundMix FAILED to load from both soft ref and LoadObject!"));
		return;
	}

	// Find a game world
	UWorld* World = nullptr;
	if (GEngine)
	{
		UE_LOG(LogTemp, Log, TEXT("[AudioDebug] WorldContexts count: %d"), GEngine->GetWorldContexts().Num());
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World())
			{
				UE_LOG(LogTemp, Log, TEXT("[AudioDebug] Found world: %s, Type=%d"), *Context.World()->GetName(), (int32)Context.WorldType);
				if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
				{
					World = Context.World();
					break;
				}
			}
		}
	}

	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[AudioDebug] No Game/PIE world found!"));
		return;
	}

	// Check audio device
	FAudioDeviceHandle AudioDevice = World->GetAudioDevice();
	UE_LOG(LogTemp, Log, TEXT("[AudioDebug] AudioDevice: %s"), AudioDevice.IsValid() ? TEXT("OK") : TEXT("NULL/INVALID"));

	// Use SetBaseSoundMix instead of PushSoundMixModifier
	UGameplayStatics::SetBaseSoundMix(World, SoundMix);
	UE_LOG(LogTemp, Log, TEXT("[AudioDebug] SetBaseSoundMix done for world: %s"), *World->GetName());

	// IMPORTANT: Never set volume to exactly 0.0 - UE virtualizes (kills) sounds at zero volume
	// and they won't resume when volume increases. Use 0.001 as minimum.
	const float MinVolume = 0.001f;

	if (MusicClass)
	{
		const float FinalMusicVolume = FMath::Max(MusicVolume * MasterVolume, MinVolume);
		UGameplayStatics::SetSoundMixClassOverride(World, SoundMix, MusicClass, FinalMusicVolume, 1.0f, 0.0f, true);
		UE_LOG(LogTemp, Log, TEXT("[AudioDebug] Music: %.4f"), FinalMusicVolume);
	}

	if (SFXClass)
	{
		const float FinalSFXVolume = FMath::Max(SFXVolume * MasterVolume, MinVolume);
		UGameplayStatics::SetSoundMixClassOverride(World, SoundMix, SFXClass, FinalSFXVolume, 1.0f, 0.0f, true);
		UE_LOG(LogTemp, Log, TEXT("[AudioDebug] SFX: %.4f"), FinalSFXVolume);
	}

	if (VoiceClass)
	{
		const float FinalVoiceVolume = FMath::Max(VoiceVolume * MasterVolume, MinVolume);
		UGameplayStatics::SetSoundMixClassOverride(World, SoundMix, VoiceClass, FinalVoiceVolume, 1.0f, 0.0f, true);
		UE_LOG(LogTemp, Log, TEXT("[AudioDebug] Voice: %.4f"), FinalVoiceVolume);
	}

	UE_LOG(LogTemp, Log, TEXT("[AudioDebug] === ApplyAudioSettings done ==="));
}

void UShooterGameSettings::ApplyGameplaySettings()
{
	// ScreenShakeIntensity is read by CameraShakeComponent.
	//
	// FOV deliberately does NOT go through APlayerCameraManager::SetFOV. That call looks harmless
	// and is anything but: it fills LockedFOV, and LockedFOV is not confined to GetFOVAngle() the
	// way reading PlayerCameraManager.cpp alone suggests. ULocalPlayer::GetViewPoint builds the
	// rendered view by taking the camera cache and then OVERWRITING the FOV with GetFOVAngle(),
	// which returns LockedFOV whenever it is above zero (LocalPlayer.cpp:715). One call here
	// therefore PINS the rendered FOV for the rest of the session: pressing Apply in the options
	// menu killed aim zoom outright, and it stayed dead until the level was reloaded, which is
	// exactly how the bug was reported.
	//
	// It is worse than a stuck number. LockedFOV overrides FOV alone, while FirstPersonFieldOfView
	// keeps arriving from the camera cache, so the two drift apart — the precise desync that puts
	// the weapon out of the hands when looking up and down. The whole reason the two are mirrored
	// in the first place is to prevent that.
	//
	// The setting reaches the renderer the honest way instead: AShooterCharacter::UpdateADS reads
	// UShooterSettingsSubsystem::GetFieldOfView() every frame and drives the camera component, and
	// UCameraShakeComponent::ApplyToCamera mirrors the first person FOV onto it. Nothing needs to
	// be pushed from here at all.
	//
	// UnlockFOV releases a lock left behind by an older build or an earlier Apply in this session.
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World())
			{
				if (APlayerController* PC = CoopPlayers::GetLocalController(World))
				{
					if (APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
					{
						CameraManager->UnlockFOV();
					}
				}
			}
		}
	}
}

float UShooterGameSettings::GetDegreesPerCount() const
{
	// This is the whole scale, and it is the definition of the number in the menu:
	// sensitivity 1.0 turns 0.022 degrees per count, exactly as it does in Apex.
	return ApexYawPerCount * LookSensitivity;
}

float UShooterGameSettings::GetInchesPer360() const
{
	const float DegreesPerCount = GetDegreesPerCount();
	if (DegreesPerCount <= KINDA_SMALL_NUMBER || MouseDpi <= 0)
	{
		return 0.0f;
	}

	const float CountsPer360 = 360.0f / DegreesPerCount;
	return CountsPer360 / static_cast<float>(MouseDpi);
}

float UShooterGameSettings::GetCentimetersPer360() const
{
	return GetInchesPer360() * 2.54f;
}

float UShooterGameSettings::GetEffectiveDpi() const
{
	return LookSensitivity * static_cast<float>(MouseDpi);
}

FText UShooterGameSettings::GetSensitivityReadout() const
{
	// The sensitivity itself is deliberately absent: the spin box next to this line already shows
	// it, and a number printed twice is a number that can look like it disagrees with itself.
	// What belongs here is what the player cannot read off the setting - what it means on the desk.
	return FText::FromString(FString::Printf(
		TEXT("%.1f cm/360   |   eDPI %.0f"),
		GetCentimetersPer360(),
		GetEffectiveDpi()));
}

void UShooterGameSettings::CalibrateFromMeasured360(float MeasuredCentimeters)
{
	// The engine says one Enhanced Input unit is one mouse count, and for a captured mouse it is.
	// A measured 360 outranks that claim: mouse drivers, pointer speed and a scaled desktop all
	// sit between the sensor and FSceneViewport. So we solve for the real ratio and store it,
	// and from then on the number in the menu is the number on the mousepad.
	if (MeasuredCentimeters <= KINDA_SMALL_NUMBER || MouseDpi <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LOOK] Calibration needs a positive measured 360 and a DPI."));
		return;
	}

	const float AppliedYawScale = GetDegreesPerCount() / FMath::Max(LookUnitsPerCount, KINDA_SMALL_NUMBER);
	if (AppliedYawScale <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float MeasuredCounts = (MeasuredCentimeters / 2.54f) * static_cast<float>(MouseDpi);
	const float MeasuredDegreesPerCount = 360.0f / MeasuredCounts;

	// Two things move together, and they have to. The measurement says what one count is really
	// worth, which fixes the units ratio; and the aim the player just measured is the aim he
	// wants to keep, so the label moves onto it instead of the aim moving onto the label.
	LookUnitsPerCount = FMath::Clamp(MeasuredDegreesPerCount / AppliedYawScale, 0.001f, 100.0f);
	LookSensitivity = FMath::Clamp(MeasuredDegreesPerCount / ApexYawPerCount, 0.05f, 20.0f);

	UE_LOG(LogTemp, Log, TEXT("[LOOK] Calibrated on %.1f cm: one input unit is %.4f mouse counts. %s"),
		MeasuredCentimeters, LookUnitsPerCount, *GetSensitivityReadout().ToString());

	ApplyControlSettings();
	SaveSettings();
}

void UShooterGameSettings::MigrateLegacySensitivity()
{
	if (bLookSensitivityMigrated)
	{
		return;
	}

	bLookSensitivityMigrated = true;

	// The old chain multiplied a hardcoded 2.5 by MouseSensitivity and the X trim, and handed
	// that straight to InputYawScale, which is degrees per input unit. Dividing by m_yaw turns
	// the same feel into the same number on Apex's scale, so nobody's aim moves on this update.
	const float LegacyDegreesPerCount = 2.5f * MouseSensitivity * MouseSensitivityX;
	const float Carried = LegacyDegreesPerCount / ApexYawPerCount;

	// Carrying the feel over is only worth doing while the feel was a real one. The old number
	// went through a hardcoded 2.5 that nobody could read off the menu, so it could just as
	// easily be a slider somebody dragged at random. A carried value outside the playable range
	// is that case, and reproducing it faithfully would only preserve nonsense: take the default.
	if (Carried >= 0.05f && Carried <= 20.0f)
	{
		LookSensitivity = Carried;
		UE_LOG(LogTemp, Log, TEXT("[LOOK] Carried the old sensitivity %.4f onto the Apex scale: %.2f."),
			MouseSensitivity, LookSensitivity);
	}
	else
	{
		// Spelled out rather than left to "whatever LoadConfig happened not to overwrite".
		LookSensitivity = 2.0f;
		UE_LOG(LogTemp, Warning,
			TEXT("[LOOK] The old sensitivity %.4f carries to %.2f on the Apex scale, which is off any "
			     "playable range - it was never a tuned value. Falling back to the default %.2f."),
			MouseSensitivity, Carried, LookSensitivity);
	}
}

static float GetEngineMouseAxisScale(APlayerController* PC)
{
	// Enhanced Input does not hand the pawn the raw mouse count. UEnhancedInputSubsystemInterface,
	// while it rebuilds the control mappings, reads the AxisConfig entry for the mapped key and,
	// whenever its Sensitivity is not 1, INJECTS a UInputModifierScalar carrying that value into
	// the mapping (EnhancedInputSubsystemInterface.cpp:672). The project ships Mouse2D at 0.07, so
	// every count arrives as 0.07 of a unit - a factor of 14.29 that is invisible in the mapping
	// asset, because it was never authored there.
	//
	// Asking the engine for the number beats hardcoding it: change the ini and the aim stays true.
	if (PC && PC->PlayerInput)
	{
		FInputAxisProperties AxisProperties;
		if (PC->PlayerInput->GetAxisProperties(EKeys::Mouse2D, AxisProperties)
			&& AxisProperties.Sensitivity > KINDA_SMALL_NUMBER)
		{
			return AxisProperties.Sensitivity;
		}
	}

	return 1.0f;
}

void UShooterGameSettings::ApplyControlSettings()
{
	MigrateLegacySensitivity();

	// One number reaches the view, and it reaches it as degrees. Two scales stand between a mouse
	// count and the turn, so both have to be divided out for the menu's number to mean what it
	// means in Apex: the engine's own mouse axis scale, and any leftover units-per-count override.
	const float DegreesPerCount = GetDegreesPerCount();

	if (!GEngine)
	{
		return;
	}

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (UWorld* World = Context.World())
		{
			if (APlayerController* PC = CoopPlayers::GetLocalController(World))
			{
				const float Units = FMath::Max(GetEngineMouseAxisScale(PC) * LookUnitsPerCount, KINDA_SMALL_NUMBER);
				const float BaseScale = DegreesPerCount / Units;

				const float YawScale = BaseScale * MouseSensitivityX * (bInvertMouseX ? -1.0f : 1.0f);
				// A negative pitch scale is the non-inverted look in UE, so the flag flips the sign
				// rather than setting it. Apex keeps m_pitch equal to m_yaw, and so do we.
				const float PitchScale = BaseScale * MouseSensitivityY * (bInvertMouseY ? 1.0f : -1.0f);

				PRAGMA_DISABLE_DEPRECATION_WARNINGS
				PC->SetDeprecatedInputYawScale(YawScale);
				PC->SetDeprecatedInputPitchScale(PitchScale);
				PRAGMA_ENABLE_DEPRECATION_WARNINGS
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
	ADSSensitivityMultiplier = 1.0f;
	LookSensitivity = 2.0f;
	MouseDpi = 800;
	// The calibration constant is deliberately NOT reset here: it describes the machine, not
	// the taste, and a player who reset his controls would otherwise lose a measured 360.
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
				if (APlayerController* PC = CoopPlayers::GetLocalController(World))
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
	CustomKeyBindings.Add(FKeyBindingEntry(FName("IA_Dash"), EKeys::LeftAlt));
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

// ==================== Console: see and set the look sensitivity ====================
//
// The menu shows the same numbers, but these read out of the running game with no widget in the
// way, which is what you want while proving the scale is real.

namespace LookSensitivityDebug
{
	static void Print(const TCHAR* Prefix)
	{
		UShooterGameSettings* Settings = UShooterGameSettings::GetShooterGameSettings();
		if (!Settings)
		{
			return;
		}

		// The console gets the full picture, the menu gets the readable half.
		const FString Line = FString::Printf(TEXT("%ssens %.2f   |   %s   |   %.4f deg/count"),
			Prefix,
			Settings->LookSensitivity,
			*Settings->GetSensitivityReadout().ToString(),
			Settings->GetDegreesPerCount());
		UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.0f, FColor::Cyan, Line);
		}
	}

	static void CmdShow()
	{
		Print(TEXT("[LOOK] "));
	}

	static void CmdSet(const TArray<FString>& Args)
	{
		UShooterGameSettings* Settings = UShooterGameSettings::GetShooterGameSettings();
		if (!Settings || Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LOOK] Usage: polarity.sens.set <value>"));
			return;
		}

		Settings->LookSensitivity = FMath::Clamp(FCString::Atof(*Args[0]), 0.05f, 20.0f);
		Settings->ApplyControlSettings();
		Settings->SaveSettings();
		Print(TEXT("[LOOK] set -> "));
	}

	static void CmdDpi(const TArray<FString>& Args)
	{
		UShooterGameSettings* Settings = UShooterGameSettings::GetShooterGameSettings();
		if (!Settings || Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LOOK] Usage: polarity.sens.dpi <value>"));
			return;
		}

		Settings->MouseDpi = FMath::Clamp(FCString::Atoi(*Args[0]), 100, 32000);
		Settings->SaveSettings();
		Print(TEXT("[LOOK] dpi -> "));
	}

	static void CmdCalibrate(const TArray<FString>& Args)
	{
		UShooterGameSettings* Settings = UShooterGameSettings::GetShooterGameSettings();
		if (!Settings || Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LOOK] Usage: polarity.sens.calibrate <measured cm for one 360>"));
			return;
		}

		Settings->CalibrateFromMeasured360(FCString::Atof(*Args[0]));
		Print(TEXT("[LOOK] calibrated -> "));
	}
}

static FAutoConsoleCommand GPolaritySensShowCmd(
	TEXT("polarity.sens"),
	TEXT("Show the look sensitivity: Apex value, cm/360, eDPI, degrees per mouse count."),
	FConsoleCommandDelegate::CreateStatic(&LookSensitivityDebug::CmdShow)
);

static FAutoConsoleCommand GPolaritySensSetCmd(
	TEXT("polarity.sens.set"),
	TEXT("Set the look sensitivity on the Apex scale. Usage: polarity.sens.set <value>"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&LookSensitivityDebug::CmdSet)
);

static FAutoConsoleCommand GPolaritySensDpiCmd(
	TEXT("polarity.sens.dpi"),
	TEXT("Tell the game your mouse DPI so cm/360 and eDPI mean something. Usage: polarity.sens.dpi <value>"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&LookSensitivityDebug::CmdDpi)
);

static FAutoConsoleCommand GPolaritySensCalibrateCmd(
	TEXT("polarity.sens.calibrate"),
	TEXT("Measure one real 360 on the mousepad and type the centimetres; the readout stops guessing. "
	     "Usage: polarity.sens.calibrate <cm>"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&LookSensitivityDebug::CmdCalibrate)
);
