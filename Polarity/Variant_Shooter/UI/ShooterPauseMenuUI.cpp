// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterPauseMenuUI.h"
#include "ShooterOptionsMenuUI.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

void UShooterPauseMenuUI::ResumeGame()
{
	// Close options menu first if open
	CloseOptionsMenu();

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetPause(false);
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());

		RemoveFromParent();
	}
}

void UShooterPauseMenuUI::RestartFromCheckpoint()
{
	// Close options menu first if open
	CloseOptionsMenu();

	OnRestartFromCheckpointRequested.Broadcast();

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetPause(false);
		RemoveFromParent();
	}
}

void UShooterPauseMenuUI::OpenSettings()
{
	// Spawn options menu if we have a class configured
	if (OptionsMenuWidgetClass && !OptionsMenuWidget)
	{
		OptionsMenuWidget = CreateWidget<UShooterOptionsMenuUI>(GetOwningPlayer(), OptionsMenuWidgetClass);
		if (OptionsMenuWidget)
		{
			OptionsMenuWidget->AddToViewport(100);
			// Subscribe to close event so we know when to show pause menu again
			OptionsMenuWidget->OnOptionsMenuClosed.AddDynamic(this, &UShooterPauseMenuUI::OnOptionsMenuClosedHandler);
		}
	}
	else if (OptionsMenuWidget)
	{
		OptionsMenuWidget->SetVisibility(ESlateVisibility::Visible);
	}

	// Hide pause menu while options are open
	SetVisibility(ESlateVisibility::Hidden);

	// Broadcast event for Blueprint to handle any additional setup
	BP_OnOpenSettings();
}

void UShooterPauseMenuUI::QuitToMainMenu()
{
	// Close options menu first if open
	CloseOptionsMenu();

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetPause(false);

		// TODO: Replace with actual main menu level name
		UGameplayStatics::OpenLevel(this, FName("MainMenu"));
	}
}

bool UShooterPauseMenuUI::IsOptionsMenuOpen() const
{
	return OptionsMenuWidget && OptionsMenuWidget->IsVisible();
}

void UShooterPauseMenuUI::CloseOptionsMenu()
{
	if (OptionsMenuWidget)
	{
		OptionsMenuWidget->OnOptionsMenuClosed.RemoveDynamic(this, &UShooterPauseMenuUI::OnOptionsMenuClosedHandler);
		OptionsMenuWidget->RemoveFromParent();
		OptionsMenuWidget = nullptr;

		// Show pause menu again
		SetVisibility(ESlateVisibility::Visible);

		BP_OnSettingsClosed();
	}
}

void UShooterPauseMenuUI::OnOptionsMenuClosedHandler()
{
	// Options menu closed itself via Back button
	OptionsMenuWidget = nullptr;

	// Show pause menu again
	SetVisibility(ESlateVisibility::Visible);

	BP_OnSettingsClosed();
}
