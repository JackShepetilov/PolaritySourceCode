// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterMainMenuUI.h"
#include "ShooterOptionsMenuUI.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Variant_Shooter/Run/RunSubsystem.h"
#include "Variant_Shooter/Run/Generation/BiomeRunRegistry.h"

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

void UShooterMainMenuUI::NativeDestruct()
{
	// Broadcast before destruction so Level BP can react
	OnMainMenuRemoved.Broadcast();

	// Clean up options menu if still exists
	if (OptionsMenuWidget)
	{
		OptionsMenuWidget->OnOptionsMenuClosed.RemoveDynamic(this, &UShooterMainMenuUI::OnOptionsMenuClosedHandler);
		OptionsMenuWidget->RemoveFromParent();
		OptionsMenuWidget = nullptr;
	}

	Super::NativeDestruct();
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

void UShooterMainMenuUI::StartNewRun()
{
	if (!NewRunBiomeRegistry)
	{
		UE_LOG(LogTemp, Error, TEXT("[RUN_FLOW] StartNewRun failed: assign NewRunBiomeRegistry in WBP_MainMenu"));
		return;
	}

	UGameInstance* GI = GetGameInstance();
	URunSubsystem* Run = GI ? GI->GetSubsystem<URunSubsystem>() : nullptr;
	if (!Run)
	{
		UE_LOG(LogTemp, Error, TEXT("[RUN_FLOW] StartNewRun failed: RunSubsystem is unavailable"));
		return;
	}

	Run->OpenNewRunFromBiome(NewRunBiomeRegistry, RunLoadingScreenClass, RunLoadingSpinnerTexture);
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
		BP_OnSettingsClosed();
	}
}

void UShooterMainMenuUI::OnOptionsMenuClosedHandler()
{
	// Options menu closed itself via Back button
	OptionsMenuWidget = nullptr;
	BP_OnSettingsClosed();
}
