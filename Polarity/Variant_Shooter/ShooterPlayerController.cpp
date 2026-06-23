// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerStart.h"
#include "ShooterCharacter.h"
#include "ShooterBulletCounterUI.h"
#include "AbilityResourceBar.h"
#include "CrosshairWidget.h"
#include "MeleeAttackComponent.h"
#include "Polarity.h"
#include "TutorialSubsystem.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "RunSubsystem.h"
#include "UpgradeChoiceWidget.h"

void AShooterPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// only spawn touch controls on local player controllers
	if (IsLocalPlayerController())
	{
		if (SVirtualJoystick::ShouldDisplayTouchInterface())
		{
			// spawn the mobile controls widget
			MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

			if (MobileControlsWidget)
			{
				// add the controls to the player screen
				MobileControlsWidget->AddToPlayerScreen(0);

			}
			else {

				UE_LOG(LogPolarity, Error, TEXT("Could not spawn mobile controls widget."));

			}
		}

		// create the bullet counter widget and add it to the screen
		BulletCounterUI = CreateWidget<UShooterBulletCounterUI>(this, BulletCounterUIClass);

		if (BulletCounterUI)
		{
			BulletCounterUI->AddToPlayerScreen(0);

			// Register HUD widget with tutorial subsystem for arrow display
			if (UGameInstance* GI = GetGameInstance())
			{
				if (UTutorialSubsystem* TutorialSub = GI->GetSubsystem<UTutorialSubsystem>())
				{
					TutorialSub->SetHUDWidget(BulletCounterUI);
				}
			}
		}
		else {

			UE_LOG(LogPolarity, Error, TEXT("Could not spawn bullet counter widget."));

		}

		// create the ability/resource bar widget and add it to the screen
		if (AbilityResourceBarClass)
		{
			AbilityResourceBar = CreateWidget<UAbilityResourceBar>(this, AbilityResourceBarClass);
			if (AbilityResourceBar)
			{
				AbilityResourceBar->AddToPlayerScreen(0);

				// OnPossess for the starting pawn usually fires BEFORE this BeginPlay, so the bar
				// didn't exist yet to bind there. If we're already possessing a character, bind now.
				// InitializeFor is idempotent (unbinds first), so the OnPossess path stays safe for respawns.
				if (AShooterCharacter* PossessedCharacter = Cast<AShooterCharacter>(GetPawn()))
				{
					UE_LOG(LogTemp, Warning, TEXT("[ABILITY_BAR] BeginPlay: binding bar to already-possessed pawn %s"), *GetNameSafe(PossessedCharacter));
					AbilityResourceBar->InitializeFor(PossessedCharacter);
				}
			}
			else
			{
				UE_LOG(LogPolarity, Error, TEXT("Could not spawn ability/resource bar widget."));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[ABILITY_BAR] AbilityResourceBarClass is NOT set on ShooterPlayerController -> bar will never be created. Set it on the BP_ShooterPlayerController."));
		}

		// create the crosshair widget and add it to the screen
		if (CrosshairWidgetClass)
		{
			CrosshairWidget = CreateWidget<UCrosshairWidget>(this, CrosshairWidgetClass);
			if (CrosshairWidget)
			{
				CrosshairWidget->AddToPlayerScreen(0);

				// OnPossess for the starting pawn usually fires BEFORE this BeginPlay, so the widget
				// didn't exist yet to receive the initial weapon. If we're already possessing a
				// character, push its current weapon now (nullptr = unarmed -> idle dot).
				if (AShooterCharacter* PossessedCharacter = Cast<AShooterCharacter>(GetPawn()))
				{
					CrosshairWidget->SetActiveWeapon(PossessedCharacter->GetCurrentWeapon());
				}
			}
			else
			{
				UE_LOG(LogPolarity, Error, TEXT("Could not spawn crosshair widget."));
			}
		}

		// Setup IMCs and key remapping
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			TSet<UInputMappingContext*> AllContexts;

			// Collect all IMCs first
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				if (CurrentContext)
				{
					AllContexts.Add(CurrentContext);
				}
			}
			if (!SVirtualJoystick::ShouldDisplayTouchInterface())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					if (CurrentContext)
					{
						AllContexts.Add(CurrentContext);
					}
				}
			}

			// Register FIRST (before AddMappingContext)
			if (UEnhancedInputUserSettings* UserSettings = Subsystem->GetUserSettings())
			{
				UserSettings->RegisterInputMappingContexts(AllContexts);
				UE_LOG(LogPolarity, Log, TEXT("ShooterPlayerController: Registered %d IMCs"), AllContexts.Num());
			}

			// THEN add mapping contexts
			for (UInputMappingContext* CurrentContext : AllContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}
			UE_LOG(LogPolarity, Log, TEXT("ShooterPlayerController: Added %d IMCs"), AllContexts.Num());
		}

		// Subscribe to RunSubsystem to (de)spawn roguelite HUD widgets on run start/end
		if (UGameInstance* GI = GetGameInstance())
		{
			if (URunSubsystem* Run = GI->GetSubsystem<URunSubsystem>())
			{
				Run->OnRunStarted.AddDynamic(this, &AShooterPlayerController::HandleRunStarted);
				Run->OnRunEnded.AddDynamic(this, &AShooterPlayerController::HandleRunEnded);

				// If run is already active when PC spawns (e.g., level reload mid-run), create widgets now
				if (Run->IsRunActive())
				{
					CreateRunWidgets();
				}
			}
		}
	}
}

void AShooterPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (URunSubsystem* Run = GI->GetSubsystem<URunSubsystem>())
		{
			Run->OnRunStarted.RemoveDynamic(this, &AShooterPlayerController::HandleRunStarted);
			Run->OnRunEnded.RemoveDynamic(this, &AShooterPlayerController::HandleRunEnded);
		}
	}
	DestroyRunWidgets();
	Super::EndPlay(Reason);
}

void AShooterPlayerController::HandleRunStarted()
{
	UE_LOG(LogPolarity, Log, TEXT("[RUN_DEBUG] PC: HandleRunStarted -> creating widgets"));
	CreateRunWidgets();
}

void AShooterPlayerController::HandleRunEnded(ERunEndReason Reason)
{
	UE_LOG(LogPolarity, Log, TEXT("[RUN_DEBUG] PC: HandleRunEnded reason=%d -> destroying widgets"), (int32)Reason);
	DestroyRunWidgets();
}

void AShooterPlayerController::CreateRunWidgets()
{
	if (!IsLocalPlayerController()) return;

	if (XPBarWidgetClass && !XPBarWidget)
	{
		XPBarWidget = CreateWidget<UUserWidget>(this, XPBarWidgetClass);
		if (XPBarWidget)
		{
			XPBarWidget->AddToPlayerScreen(0);
		}
	}

	if (UpgradeChoiceWidgetClass && !UpgradeChoiceWidget)
	{
		UpgradeChoiceWidget = CreateWidget<UUpgradeChoiceWidget>(this, UpgradeChoiceWidgetClass);
		if (UpgradeChoiceWidget)
		{
			// Higher Z so it renders above HUD when shown
			UpgradeChoiceWidget->AddToPlayerScreen(10);
		}
	}
}

void AShooterPlayerController::DestroyRunWidgets()
{
	if (XPBarWidget)
	{
		XPBarWidget->RemoveFromParent();
		XPBarWidget = nullptr;
	}
	if (UpgradeChoiceWidget)
	{
		UpgradeChoiceWidget->RemoveFromParent();
		UpgradeChoiceWidget = nullptr;
	}
}

void AShooterPlayerController::SetRunHUDVisible(bool bVisible, bool bAnimated)
{
	// Widgets (HP/charge counter, ability bar, …) bound to OnRunHUDVisibilityChanged decide how to
	// show/hide; bAnimated lets the cutscene pick a fade animation vs an instant snap per call.
	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] SetRunHUDVisible visible=%d animated=%d"), bVisible ? 1 : 0, bAnimated ? 1 : 0);
	OnRunHUDVisibilityChanged.Broadcast(bVisible, bAnimated);
}

void AShooterPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// NOTE: IMC setup moved to BeginPlay() to ensure AddMappingContext and
	// RegisterInputMappingContexts happen together (prevents Vector2D corruption)
}

void AShooterPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// subscribe to the pawn's OnDestroyed delegate
	InPawn->OnDestroyed.AddDynamic(this, &AShooterPlayerController::OnPawnDestroyed);

	// is this a shooter character?
	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(InPawn))
	{
		// add the player tag
		ShooterCharacter->Tags.Add(PlayerPawnTag);

		// subscribe to the pawn's delegates
		ShooterCharacter->OnBulletCountUpdated.AddDynamic(this, &AShooterPlayerController::OnBulletCountUpdated);
		ShooterCharacter->OnDamaged.AddDynamic(this, &AShooterPlayerController::OnPawnDamaged);
		ShooterCharacter->OnDamageDirection.AddDynamic(this, &AShooterPlayerController::OnDamageDirection);
		ShooterCharacter->OnHeatUpdated.AddDynamic(this, &AShooterPlayerController::OnHeatUpdated);
		ShooterCharacter->OnSpeedUpdated.AddDynamic(this, &AShooterPlayerController::OnSpeedUpdated);
		ShooterCharacter->OnPolarityChanged.AddDynamic(this, &AShooterPlayerController::OnPolarityChanged);
		ShooterCharacter->OnChargeUpdated.AddDynamic(this, &AShooterPlayerController::OnChargeUpdated);
		ShooterCharacter->OnMeleeWeaponEquipped.AddDynamic(this, &AShooterPlayerController::OnMeleeWeaponEquipped);
		ShooterCharacter->OnActiveWeaponChanged.AddDynamic(this, &AShooterPlayerController::OnActiveWeaponChanged);

		// Bind melee component events directly for drop kick cooldown UI
		if (UMeleeAttackComponent* MeleeComp = ShooterCharacter->GetMeleeAttackComponent())
		{
			MeleeComp->OnDropKickCooldownStarted.AddDynamic(this, &AShooterPlayerController::OnDropKickCooldownStarted);
			MeleeComp->OnDropKickCooldownEnded.AddDynamic(this, &AShooterPlayerController::OnDropKickCooldownEnded);
			MeleeComp->OnMeleeCooldownStarted.AddDynamic(this, &AShooterPlayerController::OnMeleeCooldownStarted);
			MeleeComp->OnMeleeCooldownEnded.AddDynamic(this, &AShooterPlayerController::OnMeleeCooldownEnded);
			MeleeComp->OnMeleeChargeChanged.AddDynamic(this, &AShooterPlayerController::OnMeleeChargeChanged);
		}

		// Rebind UI widget to new character (for HitMarker after respawn)
		if (BulletCounterUI)
		{
			BulletCounterUI->BP_BindToCharacter(ShooterCharacter);
		}

		// Bind the ability/resource bar to the (re)possessed character. InitializeFor unbinds the
		// previous character first, so this is respawn-safe.
		if (AbilityResourceBar)
		{
			AbilityResourceBar->InitializeFor(ShooterCharacter);
		}

		// Sync the crosshair to the (re)possessed character's current weapon. On a respawn the widget
		// already exists (BeginPlay ran); on the very first possess it doesn't yet, and the BeginPlay
		// block above pushes the starting weapon instead.
		if (CrosshairWidget)
		{
			CrosshairWidget->SetActiveWeapon(ShooterCharacter->GetCurrentWeapon());
		}

		// force update the life bar + armor bar
		ShooterCharacter->OnDamaged.Broadcast(1.0f, ShooterCharacter->GetMaxArmor() > 0.0f ? ShooterCharacter->GetCurrentArmor() / ShooterCharacter->GetMaxArmor() : 0.0f);
	}
}

void AShooterPlayerController::OnPawnDestroyed(AActor* DestroyedActor)
{
	// reset the bullet counter HUD
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_UpdateBulletCounter(0, 0);
	}

	// find the player start
	TArray<AActor*> ActorList;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), ActorList);

	if (ActorList.Num() > 0)
	{
		// select a random player start
		AActor* RandomPlayerStart = ActorList[FMath::RandRange(0, ActorList.Num() - 1)];

		// spawn a character at the player start
		const FTransform SpawnTransform = RandomPlayerStart->GetActorTransform();

		if (AShooterCharacter* RespawnedCharacter = GetWorld()->SpawnActor<AShooterCharacter>(CharacterClass, SpawnTransform))
		{
			// possess the character
			Possess(RespawnedCharacter);
		}
	}
}

void AShooterPlayerController::OnBulletCountUpdated(int32 MagazineSize, int32 Bullets)
{
	// update the UI
	if (BulletCounterUI)
	{
		BulletCounterUI->BP_UpdateBulletCounter(MagazineSize, Bullets);
	}
}

void AShooterPlayerController::OnActiveWeaponChanged(AShooterWeapon* NewWeapon)
{
	// Forward to the crosshair: NewWeapon drives armed/unarmed + which config to show.
	if (CrosshairWidget)
	{
		CrosshairWidget->SetActiveWeapon(NewWeapon);
	}
}

void AShooterPlayerController::OnPawnDamaged(float LifePercent, float ArmorPercent)
{
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_Damaged(LifePercent, ArmorPercent);
	}
}

void AShooterPlayerController::OnDamageDirection(float AngleDegrees, float Damage)
{
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_ShowDamageDirection(AngleDegrees, Damage);
	}
}

void AShooterPlayerController::OnHeatUpdated(float HeatPercent, float DamageMultiplier)
{
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_UpdateHeat(HeatPercent, DamageMultiplier);
	}
}

void AShooterPlayerController::OnSpeedUpdated(float SpeedPercent, float CurrentSpeed, float MaxSpeed)
{
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_UpdateSpeed(SpeedPercent, CurrentSpeed, MaxSpeed);
	}
}

void AShooterPlayerController::OnPolarityChanged(uint8 NewPolarity, float ChargeValue)
{
	if (IsValid(BulletCounterUI))
	{
		// Convert uint8 to EChargePolarity enum
		EChargePolarity Polarity = static_cast<EChargePolarity>(NewPolarity);
		BulletCounterUI->BP_OnPolarityChanged(Polarity, ChargeValue);
	}
}

void AShooterPlayerController::OnChargeUpdated(float ChargeValue, uint8 Polarity)
{
	if (IsValid(BulletCounterUI))
	{
		EChargePolarity PolarityEnum = static_cast<EChargePolarity>(Polarity);
		BulletCounterUI->BP_UpdateCharge(ChargeValue, PolarityEnum);
	}
}

void AShooterPlayerController::OnDropKickCooldownStarted(float CooldownDuration)
{
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_OnDropKickCooldownStarted(CooldownDuration);
	}
}

void AShooterPlayerController::OnDropKickCooldownEnded()
{
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_OnDropKickCooldownEnded();
	}
}

void AShooterPlayerController::OnMeleeCooldownStarted(float TotalCooldownDuration)
{
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_OnMeleeCooldownStarted(TotalCooldownDuration);
	}
}

void AShooterPlayerController::OnMeleeCooldownEnded()
{
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_OnMeleeCooldownEnded();
	}
}

void AShooterPlayerController::OnMeleeChargeChanged(int32 CurrentCharges, int32 MaxCharges)
{
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_OnMeleeChargeChanged(CurrentCharges, MaxCharges);
	}
}

void AShooterPlayerController::OnMeleeWeaponEquipped(bool bEquipped, int32 RemainingHits, int32 MaxHits)
{
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_OnMeleeWeaponEquipped(bEquipped, RemainingHits, MaxHits);
	}
}