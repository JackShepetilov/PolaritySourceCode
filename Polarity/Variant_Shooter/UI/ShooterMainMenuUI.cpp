// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterMainMenuUI.h"
#include "ShooterOptionsMenuUI.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/PlayerController.h"

void UShooterMainMenuUI::NativeConstruct()
{
	Super::NativeConstruct();

	// Setup input mode for menu
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetShowMouseCursor(true);
		PC->SetInputMode(FInputModeUIOnly());
	}

	BP_OnMenuShown();
}

void UShooterMainMenuUI::OpenSettings()
{
	// Spawn options menu if we have a class configured
	if (OptionsMenuWidgetClass && !OptionsMenuWidget)
	{
		OptionsMenuWidget = CreateWidget<UShooterOptionsMenuUI>(GetOwningPlayer(), OptionsMenuWidgetClass);
		if (OptionsMenuWidget)
		{
			OptionsMenuWidget->AddToViewport(100);
			// Subscribe to close event so we know when to show main menu again
			OptionsMenuWidget->OnOptionsMenuClosed.AddDynamic(this, &UShooterMainMenuUI::OnOptionsMenuClosedHandler);
		}
	}
	else if (OptionsMenuWidget)
	{
		OptionsMenuWidget->SetVisibility(ESlateVisibility::Visible);
	}

	// Hide main menu while options are open
	SetVisibility(ESlateVisibility::Hidden);

	// Broadcast event for Blueprint to handle any additional setup
	BP_OnOpenSettings();
}

void UShooterMainMenuUI::StartTutorial()
{
	// Always broadcast the event so Blueprint can react
	BP_OnTutorialRequested();

	// Optionally load the tutorial level
	if (bTutorialLoadsLevel && !TutorialLevelName.IsNone())
	{
		UGameplayStatics::OpenLevel(this, TutorialLevelName);
	}
}

void UShooterMainMenuUI::LoadLevel1()
{
	if (!Level1Name.IsNone())
	{
		UGameplayStatics::OpenLevel(this, Level1Name);
	}
}

void UShooterMainMenuUI::LoadLevel2()
{
	if (!Level2Name.IsNone())
	{
		UGameplayStatics::OpenLevel(this, Level2Name);
	}
}

void UShooterMainMenuUI::QuitGame()
{
	// Close options menu first if open
	CloseOptionsMenu();

	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

bool UShooterMainMenuUI::IsOptionsMenuOpen() const
{
	return OptionsMenuWidget && OptionsMenuWidget->IsVisible();
}

void UShooterMainMenuUI::CloseOptionsMenu()
{
	if (OptionsMenuWidget)
	{
		OptionsMenuWidget->OnOptionsMenuClosed.RemoveDynamic(this, &UShooterMainMenuUI::OnOptionsMenuClosedHandler);
		OptionsMenuWidget->RemoveFromParent();
		OptionsMenuWidget = nullptr;

		// Show main menu again
		SetVisibility(ESlateVisibility::Visible);

		BP_OnSettingsClosed();
	}
}

void UShooterMainMenuUI::OnOptionsMenuClosedHandler()
{
	// Options menu closed itself via Back button
	OptionsMenuWidget = nullptr;

	// Show main menu again
	SetVisibility(ESlateVisibility::Visible);

	BP_OnSettingsClosed();
}
