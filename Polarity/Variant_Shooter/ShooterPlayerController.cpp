// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerStart.h"
#include "ShooterCharacter.h"
#include "ShooterBulletCounterUI.h"
#include "MeleeAttackComponent.h"
#include "Polarity.h"
#include "Widgets/Input/SVirtualJoystick.h"

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

		}
		else {

			UE_LOG(LogPolarity, Error, TEXT("Could not spawn bullet counter widget."));

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
	}
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

		// Bind melee component events directly for drop kick cooldown UI
		if (UMeleeAttackComponent* MeleeComp = ShooterCharacter->GetMeleeAttackComponent())
		{
			MeleeComp->OnDropKickCooldownStarted.AddDynamic(this, &AShooterPlayerController::OnDropKickCooldownStarted);
			MeleeComp->OnDropKickCooldownEnded.AddDynamic(this, &AShooterPlayerController::OnDropKickCooldownEnded);
		}

		// Rebind UI widget to new character (for HitMarker after respawn)
		if (BulletCounterUI)
		{
			BulletCounterUI->BP_BindToCharacter(ShooterCharacter);
		}

		// force update the life bar
		ShooterCharacter->OnDamaged.Broadcast(1.0f);
	}
}

void AShooterPlayerController::OnPawnDestroyed(AActor* DestroyedActor)
{
	// reset the bullet counter HUD
	BulletCounterUI->BP_UpdateBulletCounter(0, 0);

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

void AShooterPlayerController::OnPawnDamaged(float LifePercent)
{
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_Damaged(LifePercent);
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