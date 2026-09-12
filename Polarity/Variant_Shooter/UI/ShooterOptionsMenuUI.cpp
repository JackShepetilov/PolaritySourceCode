// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterOptionsMenuUI.h"
#include "Variant_Shooter/ShooterGameSettings.h"
#include "Variant_Shooter/ShooterSettingsSubsystem.h"
#include "ShooterKeyBindingsUI.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameUserSettings.h"
#include "Components/Slider.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"

void UShooterOptionsMenuUI::NativeConstruct()
{
	Super::NativeConstruct();

	CurrentCategory = ESettingsCategory::Audio;
	bHasUnsavedChanges = false;

	SetupSensitivityWidgets();

	BP_OnMenuOpened();
	BP_RefreshAllUI();
}

void UShooterOptionsMenuUI::SetupSensitivityWidgets()
{
	// The ranges are written here rather than left in the asset on purpose. They belong to the
	// Apex scale, the scale is defined in code, and a slider whose bounds disagree with the code
	// is exactly the kind of quiet mismatch this whole rework exists to remove.
	constexpr float SensMin = 0.05f;
	constexpr float SensMax = 20.0f;

	if (Slider_MouseSensitivity)
	{
		Slider_MouseSensitivity->SetMinValue(SensMin);
		Slider_MouseSensitivity->SetMaxValue(SensMax);
		Slider_MouseSensitivity->SetStepSize(0.05f);
		Slider_MouseSensitivity->OnValueChanged.AddUniqueDynamic(
			this, &UShooterOptionsMenuUI::HandleSensitivitySliderChanged);
	}

	if (SpinBox_LookSensitivity)
	{
		// Typing is the point of this one, so the hard bounds and the drag bounds are the same
		// and two decimals are shown: Apex configs are quoted to two, and a field that rounds
		// 1.35 to 1.4 would silently refuse to hold the number the player came here to enter.
		SpinBox_LookSensitivity->SetMinValue(SensMin);
		SpinBox_LookSensitivity->SetMaxValue(SensMax);
		SpinBox_LookSensitivity->SetMinSliderValue(SensMin);
		SpinBox_LookSensitivity->SetMaxSliderValue(SensMax);
		SpinBox_LookSensitivity->SetMinFractionalDigits(2);
		SpinBox_LookSensitivity->SetMaxFractionalDigits(2);
		SpinBox_LookSensitivity->SetDelta(0.0f);
		SpinBox_LookSensitivity->OnValueChanged.AddUniqueDynamic(
			this, &UShooterOptionsMenuUI::HandleSensitivitySpinBoxChanged);
	}

	if (SpinBox_MouseDpi)
	{
		SpinBox_MouseDpi->SetMinValue(100.0f);
		SpinBox_MouseDpi->SetMaxValue(32000.0f);
		SpinBox_MouseDpi->SetMinSliderValue(100.0f);
		SpinBox_MouseDpi->SetMaxSliderValue(6400.0f);
		SpinBox_MouseDpi->SetMinFractionalDigits(0);
		SpinBox_MouseDpi->SetMaxFractionalDigits(0);
		SpinBox_MouseDpi->SetDelta(50.0f);
		SpinBox_MouseDpi->OnValueChanged.AddUniqueDynamic(
			this, &UShooterOptionsMenuUI::HandleMouseDpiSpinBoxChanged);
	}

	RefreshSensitivityWidgets();
}

void UShooterOptionsMenuUI::RefreshSensitivityWidgets()
{
	UShooterGameSettings* Settings = GetGameSettings();
	if (!Settings)
	{
		return;
	}

	// Writing a value into a widget can make it shout that its value changed, which would send us
	// straight back in here. The guard is what makes the slider and the spin box able to follow
	// each other instead of arguing.
	TGuardValue<bool> Guard(bUpdatingSensitivityWidgets, true);

	if (Slider_MouseSensitivity)
	{
		Slider_MouseSensitivity->SetValue(Settings->LookSensitivity);
	}

	if (SpinBox_LookSensitivity)
	{
		SpinBox_LookSensitivity->SetValue(Settings->LookSensitivity);
	}

	if (SpinBox_MouseDpi)
	{
		SpinBox_MouseDpi->SetValue(static_cast<float>(Settings->MouseDpi));
	}

	if (Text_SensitivityReadout)
	{
		Text_SensitivityReadout->SetText(Settings->GetSensitivityReadout());
	}
}

void UShooterOptionsMenuUI::HandleSensitivitySliderChanged(float Value)
{
	if (bUpdatingSensitivityWidgets)
	{
		return;
	}

	SetMouseSensitivity(Value);
	RefreshSensitivityWidgets();
}

void UShooterOptionsMenuUI::HandleSensitivitySpinBoxChanged(float Value)
{
	if (bUpdatingSensitivityWidgets)
	{
		return;
	}

	SetMouseSensitivity(Value);
	RefreshSensitivityWidgets();
}

void UShooterOptionsMenuUI::HandleMouseDpiSpinBoxChanged(float Value)
{
	if (bUpdatingSensitivityWidgets)
	{
		return;
	}

	SetMouseDpi(FMath::RoundToInt(Value));
	RefreshSensitivityWidgets();
}

void UShooterOptionsMenuUI::NativeDestruct()
{
	BP_OnMenuClosed();

	// Clean up key bindings widget if spawned
	if (KeyBindingsWidget)
	{
		KeyBindingsWidget->RemoveFromParent();
		KeyBindingsWidget = nullptr;
	}

	Super::NativeDestruct();
}

// ==================== Navigation ====================

void UShooterOptionsMenuUI::SwitchCategory(ESettingsCategory NewCategory)
{
	if (CurrentCategory != NewCategory)
	{
		CurrentCategory = NewCategory;
		BP_OnCategoryChanged(NewCategory);
	}
}

void UShooterOptionsMenuUI::CloseMenu()
{
	// If there are unsaved changes, Blueprint should handle confirmation dialog
	OnOptionsMenuClosed.Broadcast();
	RemoveFromParent();
}

void UShooterOptionsMenuUI::OpenKeyBindings()
{
	SwitchCategory(ESettingsCategory::KeyBindings);

	// Spawn key bindings widget if we have a class
	if (KeyBindingsWidgetClass && !KeyBindingsWidget)
	{
		KeyBindingsWidget = CreateWidget<UShooterKeyBindingsUI>(GetOwningPlayer(), KeyBindingsWidgetClass);
		if (KeyBindingsWidget)
		{
			KeyBindingsWidget->AddToViewport(100);
			// Subscribe to close event so we know when to show options menu again
			KeyBindingsWidget->OnKeyBindingsMenuClosed.AddDynamic(this, &UShooterOptionsMenuUI::OnKeyBindingsMenuClosedHandler);
		}
	}
	else if (KeyBindingsWidget)
	{
		KeyBindingsWidget->SetVisibility(ESlateVisibility::Visible);
	}

	// Hide options menu while key bindings are open
	SetVisibility(ESlateVisibility::Hidden);
}

// ==================== Settings Actions ====================

void UShooterOptionsMenuUI::ApplySettings()
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		// Apply and save custom settings
		Settings->ApplyAllCustomSettings();

		// Also apply graphics settings from parent class
		Settings->ApplySettings(false);
		Settings->SaveSettings();

		bHasUnsavedChanges = false;
		BP_OnSettingsApplied();
	}
}

void UShooterOptionsMenuUI::RevertSettings()
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		// Reload settings from disk
		Settings->LoadSettings();

		// The look slider previews itself while you drag it, so backing out has to put the view
		// back too. Reloading the number alone would leave the aim on the value being cancelled.
		Settings->ApplyControlSettings();

		bHasUnsavedChanges = false;
		RefreshSensitivityWidgets();
		BP_OnSettingsReverted();
		BP_RefreshAllUI();
	}
}

void UShooterOptionsMenuUI::ResetCategoryToDefaults()
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		switch (CurrentCategory)
		{
		case ESettingsCategory::Audio:
			Settings->ResetAudioToDefaults();
			break;
		case ESettingsCategory::Controls:
			Settings->ResetControlsToDefaults();
			// Same reason as Revert: the number on screen and the aim under the hand must agree.
			Settings->ApplyControlSettings();
			break;
		case ESettingsCategory::Graphics:
			Settings->SetToDefaults(); // Parent class handles graphics
			break;
		case ESettingsCategory::Gameplay:
			Settings->ResetGameplayToDefaults();
			break;
		case ESettingsCategory::KeyBindings:
			Settings->ResetKeyBindingsToDefaults();
			break;
		default:
			break;
		}

		bHasUnsavedChanges = true;
		RefreshSensitivityWidgets();
		BP_RefreshAllUI();
	}
}

void UShooterOptionsMenuUI::ResetAllToDefaults()
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->ResetToDefaults();
		Settings->ApplyControlSettings();
		bHasUnsavedChanges = true;
		RefreshSensitivityWidgets();
		BP_RefreshAllUI();
	}
}

// ==================== Audio Settings ====================

float UShooterOptionsMenuUI::GetMasterVolume() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->MasterVolume;
	}
	return 1.0f;
}

void UShooterOptionsMenuUI::SetMasterVolume(float Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->MasterVolume = FMath::Clamp(Value, 0.0f, 1.0f);
		UE_LOG(LogTemp, Log, TEXT("[AudioDebug] SetMasterVolume: %.2f"), Settings->MasterVolume);
		MarkSettingModified(FName("MasterVolume"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[AudioDebug] SetMasterVolume: GetGameSettings() returned null!"));
	}
}

float UShooterOptionsMenuUI::GetMusicVolume() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->MusicVolume;
	}
	return 1.0f;
}

void UShooterOptionsMenuUI::SetMusicVolume(float Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->MusicVolume = FMath::Clamp(Value, 0.0f, 1.0f);
		UE_LOG(LogTemp, Log, TEXT("[AudioDebug] SetMusicVolume: %.2f"), Settings->MusicVolume);
		MarkSettingModified(FName("MusicVolume"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[AudioDebug] SetMusicVolume: GetGameSettings() returned null!"));
	}
}

float UShooterOptionsMenuUI::GetSFXVolume() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->SFXVolume;
	}
	return 1.0f;
}

void UShooterOptionsMenuUI::SetSFXVolume(float Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->SFXVolume = FMath::Clamp(Value, 0.0f, 1.0f);
		UE_LOG(LogTemp, Log, TEXT("[AudioDebug] SetSFXVolume: %.2f"), Settings->SFXVolume);
		MarkSettingModified(FName("SFXVolume"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[AudioDebug] SetSFXVolume: GetGameSettings() returned null!"));
	}
}

float UShooterOptionsMenuUI::GetVoiceVolume() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->VoiceVolume;
	}
	return 1.0f;
}

void UShooterOptionsMenuUI::SetVoiceVolume(float Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->VoiceVolume = FMath::Clamp(Value, 0.0f, 1.0f);
		UE_LOG(LogTemp, Log, TEXT("[AudioDebug] SetVoiceVolume: %.2f"), Settings->VoiceVolume);
		MarkSettingModified(FName("VoiceVolume"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[AudioDebug] SetVoiceVolume: GetGameSettings() returned null!"));
	}
}

// ==================== Controls Settings ====================

float UShooterOptionsMenuUI::GetMouseSensitivity() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->LookSensitivity;
	}
	return 1.0f;
}

void UShooterOptionsMenuUI::SetMouseSensitivity(float Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		// One scale reaches the view now, and the slider writes straight onto it. Applying here
		// rather than on "Apply" is deliberate: sensitivity is judged by feel, not by a number.
		Settings->LookSensitivity = FMath::Clamp(Value, 0.05f, 20.0f);
		Settings->ApplyControlSettings();
		MarkSettingModified(FName("LookSensitivity"));
	}
}

float UShooterOptionsMenuUI::GetMouseSensitivityX() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->MouseSensitivityX;
	}
	return 1.0f;
}

void UShooterOptionsMenuUI::SetMouseSensitivityX(float Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->MouseSensitivityX = FMath::Clamp(Value, 0.1f, 10.0f);
		MarkSettingModified(FName("MouseSensitivityX"));
	}
}

float UShooterOptionsMenuUI::GetMouseSensitivityY() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->MouseSensitivityY;
	}
	return 1.0f;
}

void UShooterOptionsMenuUI::SetMouseSensitivityY(float Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->MouseSensitivityY = FMath::Clamp(Value, 0.1f, 10.0f);
		MarkSettingModified(FName("MouseSensitivityY"));
	}
}

float UShooterOptionsMenuUI::GetADSSensitivityMultiplier() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->ADSSensitivityMultiplier;
	}
	return 0.7f;
}

void UShooterOptionsMenuUI::SetADSSensitivityMultiplier(float Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->ADSSensitivityMultiplier = FMath::Clamp(Value, 0.1f, 2.0f);
		MarkSettingModified(FName("ADSSensitivityMultiplier"));
	}
}

int32 UShooterOptionsMenuUI::GetMouseDpi() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->MouseDpi;
	}
	return 800;
}

void UShooterOptionsMenuUI::SetMouseDpi(int32 Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->MouseDpi = FMath::Clamp(Value, 100, 32000);
		MarkSettingModified(FName("MouseDpi"));
	}
}

float UShooterOptionsMenuUI::GetCentimetersPer360() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->GetCentimetersPer360();
	}
	return 0.0f;
}

float UShooterOptionsMenuUI::GetEffectiveDpi() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->GetEffectiveDpi();
	}
	return 0.0f;
}

FText UShooterOptionsMenuUI::GetSensitivityReadout() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->GetSensitivityReadout();
	}
	return FText::GetEmpty();
}

bool UShooterOptionsMenuUI::GetInvertMouseY() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->bInvertMouseY;
	}
	return false;
}

void UShooterOptionsMenuUI::SetInvertMouseY(bool bValue)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->bInvertMouseY = bValue;
		MarkSettingModified(FName("InvertMouseY"));
	}
}

bool UShooterOptionsMenuUI::GetInvertMouseX() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->bInvertMouseX;
	}
	return false;
}

void UShooterOptionsMenuUI::SetInvertMouseX(bool bValue)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->bInvertMouseX = bValue;
		MarkSettingModified(FName("InvertMouseX"));
	}
}

bool UShooterOptionsMenuUI::GetToggleADS() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->bToggleADS;
	}
	return false;
}

void UShooterOptionsMenuUI::SetToggleADS(bool bValue)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->bToggleADS = bValue;
		MarkSettingModified(FName("ToggleADS"));
	}
}

bool UShooterOptionsMenuUI::GetToggleCrouch() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->bToggleCrouch;
	}
	return false;
}

void UShooterOptionsMenuUI::SetToggleCrouch(bool bValue)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->bToggleCrouch = bValue;
		MarkSettingModified(FName("ToggleCrouch"));
	}
}

bool UShooterOptionsMenuUI::GetToggleSprint() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->bToggleSprint;
	}
	return false;
}

void UShooterOptionsMenuUI::SetToggleSprint(bool bValue)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->bToggleSprint = bValue;
		MarkSettingModified(FName("ToggleSprint"));
	}
}

// ==================== Gameplay Settings ====================

float UShooterOptionsMenuUI::GetFieldOfView() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->FieldOfView;
	}
	return 90.0f;
}

void UShooterOptionsMenuUI::SetFieldOfView(float Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->FieldOfView = FMath::Clamp(Value, 60.0f, 120.0f);
		MarkSettingModified(FName("FieldOfView"));
	}
}

bool UShooterOptionsMenuUI::GetShowDamageNumbers() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->bShowDamageNumbers;
	}
	return true;
}

void UShooterOptionsMenuUI::SetShowDamageNumbers(bool bValue)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->bShowDamageNumbers = bValue;
		MarkSettingModified(FName("ShowDamageNumbers"));
	}
}

bool UShooterOptionsMenuUI::GetShowHitMarkers() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->bShowHitMarkers;
	}
	return true;
}

void UShooterOptionsMenuUI::SetShowHitMarkers(bool bValue)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->bShowHitMarkers = bValue;
		MarkSettingModified(FName("ShowHitMarkers"));
	}
}

float UShooterOptionsMenuUI::GetScreenShakeIntensity() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->ScreenShakeIntensity;
	}
	return 1.0f;
}

void UShooterOptionsMenuUI::SetScreenShakeIntensity(float Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->ScreenShakeIntensity = FMath::Clamp(Value, 0.0f, 2.0f);
		MarkSettingModified(FName("ScreenShakeIntensity"));
	}
}

int32 UShooterOptionsMenuUI::GetCrosshairType() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->CrosshairType;
	}
	return 0;
}

void UShooterOptionsMenuUI::SetCrosshairType(int32 Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->CrosshairType = Value;
		MarkSettingModified(FName("CrosshairType"));
	}
}

FLinearColor UShooterOptionsMenuUI::GetCrosshairColor() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->CrosshairColor;
	}
	return FLinearColor::White;
}

void UShooterOptionsMenuUI::SetCrosshairColor(FLinearColor Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->CrosshairColor = Value;
		MarkSettingModified(FName("CrosshairColor"));
	}
}

float UShooterOptionsMenuUI::GetCrosshairSize() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->CrosshairSize;
	}
	return 1.0f;
}

void UShooterOptionsMenuUI::SetCrosshairSize(float Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->CrosshairSize = FMath::Clamp(Value, 0.5f, 2.0f);
		MarkSettingModified(FName("CrosshairSize"));
	}
}

bool UShooterOptionsMenuUI::GetShowSpeedometer() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->bShowSpeedometer;
	}
	return true;
}

void UShooterOptionsMenuUI::SetShowSpeedometer(bool bValue)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->bShowSpeedometer = bValue;
		MarkSettingModified(FName("ShowSpeedometer"));
	}
}

// ==================== Graphics Settings ====================

TArray<FIntPoint> UShooterOptionsMenuUI::GetAvailableResolutions() const
{
	TArray<FIntPoint> Resolutions;
	TArray<FScreenResolutionRHI> ScreenResolutions;

	if (RHIGetAvailableResolutions(ScreenResolutions, true))
	{
		for (const FScreenResolutionRHI& Res : ScreenResolutions)
		{
			Resolutions.Add(FIntPoint(Res.Width, Res.Height));
		}
	}

	return Resolutions;
}

FIntPoint UShooterOptionsMenuUI::GetCurrentResolution() const
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		return Settings->GetScreenResolution();
	}
	return FIntPoint(1920, 1080);
}

void UShooterOptionsMenuUI::SetResolution(FIntPoint NewResolution)
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		Settings->SetScreenResolution(NewResolution);
		MarkSettingModified(FName("Resolution"));
	}
}

int32 UShooterOptionsMenuUI::GetFullscreenMode() const
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		return static_cast<int32>(Settings->GetFullscreenMode());
	}
	return 1; // Windowed Fullscreen
}

void UShooterOptionsMenuUI::SetFullscreenMode(int32 Mode)
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(Mode));
		MarkSettingModified(FName("FullscreenMode"));
	}
}

bool UShooterOptionsMenuUI::GetVSyncEnabled() const
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		return Settings->IsVSyncEnabled();
	}
	return false;
}

void UShooterOptionsMenuUI::SetVSyncEnabled(bool bEnabled)
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		Settings->SetVSyncEnabled(bEnabled);
		MarkSettingModified(FName("VSync"));
	}
}

int32 UShooterOptionsMenuUI::GetFrameRateLimit() const
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		return static_cast<int32>(Settings->GetFrameRateLimit());
	}
	return 0;
}

void UShooterOptionsMenuUI::SetFrameRateLimit(int32 Limit)
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		Settings->SetFrameRateLimit(static_cast<float>(Limit));
		MarkSettingModified(FName("FrameRateLimit"));
	}
}

int32 UShooterOptionsMenuUI::GetOverallQuality() const
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		return Settings->GetOverallScalabilityLevel();
	}
	return 2; // High
}

void UShooterOptionsMenuUI::SetOverallQuality(int32 Quality)
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		Settings->SetOverallScalabilityLevel(Quality);
		MarkSettingModified(FName("OverallQuality"));
	}
}

// ==================== Accessibility Settings ====================

int32 UShooterOptionsMenuUI::GetColorblindMode() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->ColorblindMode;
	}
	return 0;
}

void UShooterOptionsMenuUI::SetColorblindMode(int32 Mode)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->ColorblindMode = Mode;
		MarkSettingModified(FName("ColorblindMode"));
	}
}

float UShooterOptionsMenuUI::GetColorblindIntensity() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->ColorblindIntensity;
	}
	return 1.0f;
}

void UShooterOptionsMenuUI::SetColorblindIntensity(float Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->ColorblindIntensity = FMath::Clamp(Value, 0.0f, 1.0f);
		MarkSettingModified(FName("ColorblindIntensity"));
	}
}

bool UShooterOptionsMenuUI::GetSubtitlesEnabled() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->bEnableSubtitles;
	}
	return false;
}

void UShooterOptionsMenuUI::SetSubtitlesEnabled(bool bEnabled)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->bEnableSubtitles = bEnabled;
		MarkSettingModified(FName("Subtitles"));
	}
}

float UShooterOptionsMenuUI::GetSubtitleSize() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->SubtitleSize;
	}
	return 1.0f;
}

void UShooterOptionsMenuUI::SetSubtitleSize(float Value)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->SubtitleSize = FMath::Clamp(Value, 0.5f, 2.0f);
		MarkSettingModified(FName("SubtitleSize"));
	}
}

bool UShooterOptionsMenuUI::GetHighContrastUI() const
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		return Settings->bHighContrastUI;
	}
	return false;
}

void UShooterOptionsMenuUI::SetHighContrastUI(bool bEnabled)
{
	if (UShooterGameSettings* Settings = GetGameSettings())
	{
		Settings->bHighContrastUI = bEnabled;
		MarkSettingModified(FName("HighContrastUI"));
	}
}

// ==================== Helper Methods ====================

UShooterGameSettings* UShooterOptionsMenuUI::GetGameSettings() const
{
	return UShooterGameSettings::GetShooterGameSettings();
}

UShooterSettingsSubsystem* UShooterOptionsMenuUI::GetSettingsSubsystem() const
{
	if (UGameInstance* GI = UGameplayStatics::GetGameInstance(this))
	{
		return GI->GetSubsystem<UShooterSettingsSubsystem>();
	}
	return nullptr;
}

void UShooterOptionsMenuUI::MarkSettingModified(FName SettingName)
{
	bHasUnsavedChanges = true;
	BP_OnSettingModified(SettingName);
}

void UShooterOptionsMenuUI::OnKeyBindingsMenuClosedHandler()
{
	// Key bindings menu closed itself via Back button
	KeyBindingsWidget = nullptr;

	// Show options menu again
	SetVisibility(ESlateVisibility::Visible);
}
