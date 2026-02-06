// Copyright Epic Games, Inc. All Rights Reserved.


#include "PolarityPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "PolarityCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "Polarity.h"
#include "Widgets/Input/SVirtualJoystick.h"

APolarityPlayerController::APolarityPlayerController()
{
	// set the player camera manager class
	PlayerCameraManagerClass = APolarityCameraManager::StaticClass();
}

void APolarityPlayerController::BeginPlay()
{
	Super::BeginPlay();

	
	// only spawn touch controls on local player controllers
	if (SVirtualJoystick::ShouldDisplayTouchInterface() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogPolarity, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void APolarityPlayerController::SetupInputComponent()
{
	UE_LOG(LogPolarity, Warning, TEXT("PolarityPlayerController::SetupInputComponent() CALLED"));

	Super::SetupInputComponent();

	UE_LOG(LogPolarity, Warning, TEXT("PolarityPlayerController: IsLocalPlayerController() = %s"), IsLocalPlayerController() ? TEXT("true") : TEXT("false"));

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Context
		UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
		UE_LOG(LogPolarity, Warning, TEXT("PolarityPlayerController: EnhancedInputSubsystem = %s"), Subsystem ? TEXT("VALID") : TEXT("NULL"));

		if (Subsystem)
		{
			// Collect all IMCs that will be added
			TSet<UInputMappingContext*> AllContexts;

			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				if (CurrentContext)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
					AllContexts.Add(CurrentContext);
				}
			}

			// only add these IMCs if we're not using mobile touch input
			if (!SVirtualJoystick::ShouldDisplayTouchInterface())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					if (CurrentContext)
					{
						Subsystem->AddMappingContext(CurrentContext, 0);
						AllContexts.Add(CurrentContext);
					}
				}
			}

			// Register IMCs with EnhancedInputUserSettings for key remapping support
			// This must be done HERE, at the same time as AddMappingContext,
			// NOT later in UI code, to avoid corrupting Vector2D mappings
			if (UEnhancedInputUserSettings* UserSettings = Subsystem->GetUserSettings())
			{
				UserSettings->RegisterInputMappingContexts(AllContexts);
				UE_LOG(LogPolarity, Log, TEXT("Registered %d IMCs with EnhancedInputUserSettings for key remapping"), AllContexts.Num());
			}
			else
			{
				UE_LOG(LogPolarity, Error, TEXT("PolarityPlayerController: GetUserSettings() returned nullptr! Key remapping will NOT work. "
					"Enable 'User Settings' in Project Settings -> Enhanced Input."));
			}
		}
	}

}
