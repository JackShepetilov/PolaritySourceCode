// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerStart.h"
#include "ShooterCharacter.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/UI/Hud/HudRegistry.h"
#include "Variant_Shooter/UI/InventoryScreenWidget.h"
#include "Variant_Shooter/Map/MapScreenWidget.h"
#include "Variant_Shooter/Shop/ShopScreenWidget.h"
#include "Polarity.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "RunSubsystem.h"
#include "Variant_Shooter/ShooterGameSettings.h"
#include "UpgradeChoiceWidget.h"
#include "AI/SquadSpawn/SquadSpawnSubsystem.h"
#include "AI/SquadSpawn/SquadLoadout.h"
#include "AI/SquadSpawn/SquadScenario.h"
#include "Variant_Shooter/UI/DamageNumbersSubsystem.h"

// ==================== Squad spawn console commands ====================

// ==================== Inventory overlay ====================

AShooterPlayerController::AShooterPlayerController()
{
	// A default so the map works out of the box. Set in C++ rather than on the Blueprint because
	// the MCP layer refuses to modify class defaults from Python (it crashes the editor), and a
	// feature that needs a manual click before it does anything is a feature nobody turns on.
	// A Blueprint override still wins if one is ever set.
	MapScreenClass = UMapScreenWidget::StaticClass();
}

void AShooterPlayerController::ToggleInventoryScreen()
{
	// A remote controller has no viewport to open anything in, and the screen sends nothing to the
	// server, so there is nothing here for a non-local one to do. Guarding here rather than at the
	// input site keeps the rule with the thing it protects.
	if (!IsLocalController() || !InventoryScreen)
	{
		return;
	}

	InventoryScreen->Toggle();
}

void AShooterPlayerController::CloseInventoryScreen()
{
	if (InventoryScreen)
	{
		InventoryScreen->Close();
	}
}

bool AShooterPlayerController::IsInventoryScreenOpen() const
{
	return InventoryScreen && InventoryScreen->IsOpen();
}

bool AShooterPlayerController::OpenShopScreen(ABuildableActor* Dispenser)
{
	if (!IsLocalController() || !ShopScreen || !Dispenser)
	{
		return false;
	}
	// One cursor screen at a time: the two would fight over the input mode.
	CloseInventoryScreen();
	ShopScreen->Open(Dispenser);
	return ShopScreen->IsOpen();
}

void AShooterPlayerController::CloseShopScreen()
{
	if (ShopScreen)
	{
		ShopScreen->Close();
	}
}

bool AShooterPlayerController::IsShopScreenOpen() const
{
	return ShopScreen && ShopScreen->IsOpen();
}

void AShooterPlayerController::ToggleMapScreen()
{
	if (!IsLocalController() || !MapScreen)
	{
		return;
	}

	MapScreen->Toggle();
}

void AShooterPlayerController::CloseMapScreen()
{
	if (MapScreen)
	{
		MapScreen->Close();
	}
}

bool AShooterPlayerController::IsMapScreenOpen() const
{
	return MapScreen && MapScreen->IsOpen();
}

void AShooterPlayerController::SquadSpawn(const FString& PointTag, const FString& LoadoutPath)
{
	USquadSpawnSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<USquadSpawnSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	UObject* Loaded = FSoftObjectPath(LoadoutPath).TryLoad();
	USquadLoadout* Loadout = Cast<USquadLoadout>(Loaded);
	if (!Loadout)
	{
		UE_LOG(LogTemp, Error, TEXT("[SQUAD_DEBUG] SquadSpawn: not a USquadLoadout: '%s'"), *LoadoutPath);
		return;
	}

	const int32 Spawned = Subsystem->SpawnAtTag(FName(*PointTag), Loadout);
	UE_LOG(LogTemp, Warning, TEXT("[SQUAD_DEBUG] SquadSpawn '%s' -> %d members"), *PointTag, Spawned);
}

void AShooterPlayerController::SquadRunScenario(const FString& ScenarioPath)
{
	USquadSpawnSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<USquadSpawnSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	UObject* Loaded = FSoftObjectPath(ScenarioPath).TryLoad();
	USquadScenario* Scenario = Cast<USquadScenario>(Loaded);
	if (!Scenario)
	{
		UE_LOG(LogTemp, Error, TEXT("[SQUAD_DEBUG] SquadRunScenario: not a USquadScenario: '%s'"), *ScenarioPath);
		return;
	}

	const int32 Spawned = Subsystem->RunScenario(Scenario);
	UE_LOG(LogTemp, Warning, TEXT("[SQUAD_DEBUG] SquadRunScenario -> %d members"), Spawned);
}

void AShooterPlayerController::SquadClear()
{
	if (USquadSpawnSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<USquadSpawnSubsystem>() : nullptr)
	{
		Subsystem->ClearAll();
	}
}

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

		// The HUD proper: weapon block, crosshair, ability bar and everything else that has a place
		// on the screen. The registry builds it from the layout asset and follows this controller's
		// pawn from here on, so nothing below binds those widgets by hand.
		if (UHudRegistry* Hud = ULocalPlayer::GetSubsystem<UHudRegistry>(GetLocalPlayer()))
		{
			Hud->Build(this, HudLayout);
		}

		// create the inventory overlay. It goes on the screen once and stays there hidden: the grid
		// is at most eight squares, so building it up front costs nothing and the first press of the
		// key has no hitch. Z-order above the rest of the HUD, because it covers all of it.
		if (InventoryScreenClass)
		{
			InventoryScreen = CreateWidget<UInventoryScreenWidget>(this, InventoryScreenClass);
			if (InventoryScreen)
			{
				InventoryScreen->AddToPlayerScreen(10);

				// Same reason as the bar above: OnPossess for the starting pawn usually fires before
				// this BeginPlay. InitializeFor unbinds first, so the OnPossess path stays safe.
				if (AShooterCharacter* PossessedCharacter = Cast<AShooterCharacter>(GetPawn()))
				{
					InventoryScreen->InitializeFor(PossessedCharacter);
				}
			}
			else
			{
				UE_LOG(LogPolarity, Error, TEXT("Could not spawn inventory screen widget."));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[INV_DEBUG] InventoryScreenClass is NOT set on ShooterPlayerController -> the inventory key will do nothing. Set it on BP_ShooterPlayerController."));
		}

		// The shop. Same shape again; it is opened by the feed key at a dispenser, not by a key of
		// its own.
		if (ShopScreenClass)
		{
			ShopScreen = CreateWidget<UShopScreenWidget>(this, ShopScreenClass);
			if (ShopScreen)
			{
				ShopScreen->AddToPlayerScreen(10);
				ShopScreen->SetVisibility(ESlateVisibility::Collapsed);
				if (AShooterCharacter* PossessedCharacter = Cast<AShooterCharacter>(GetPawn()))
				{
					ShopScreen->InitializeFor(PossessedCharacter);
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[SHOP_DEBUG] ShopScreenClass is NOT set on ShooterPlayerController -> the dispenser shows the old gun menu."));
		}

		// The map. Same shape as the inventory: built once, hidden, toggled by a key. Z-order above
		// it, because a map read mid-fight covers everything by design.
		if (MapScreenClass)
		{
			MapScreen = CreateWidget<UMapScreenWidget>(this, MapScreenClass);
			if (MapScreen)
			{
				MapScreen->AddToPlayerScreen(11);
				MapScreen->SetVisibility(ESlateVisibility::Collapsed);
			}
			else
			{
				UE_LOG(LogPolarity, Error, TEXT("Could not spawn map screen widget."));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[MAP_DEBUG] MapScreenClass is NOT set on ShooterPlayerController -> the map key will do nothing. Set it on BP_ShooterPlayerController."));
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

		// The look sensitivity has to be pushed onto THIS controller, here, because nothing else
		// reliably does it. UShooterSettingsSubsystem hangs its apply on PostLoadMapWithWorld, and
		// in PIE that delegate has already fired by the time a GameInstance subsystem exists, so
		// the apply never ran and the controller kept the 2.5 baked into BP_ShooterPlayerController
		// - about 114 on the Apex scale. The symptom was deceptive: dragging the slider felt
		// correct (that path applies directly), so only a fresh session was wrong.
		if (UShooterGameSettings* Settings = UShooterGameSettings::GetShooterGameSettings())
		{
			Settings->ApplyControlSettings();
			UE_LOG(LogPolarity, Log, TEXT("[LOOK] Controller ready, sensitivity applied: %s"),
				*Settings->GetSensitivityReadout().ToString());
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

void AShooterPlayerController::Client_ReportDamageNumber_Implementation(AActor* Target, float Damage, float ArmorDamage, uint8 Flags)
{
	ShowDamageNumber(Target, Damage, ArmorDamage, Flags);
}

void AShooterPlayerController::ShowDamageNumber(AActor* Target, float Damage, float ArmorDamage, uint8 Flags)
{
	// The number is drawn for the person at this screen, and this controller is theirs.
	if (!IsLocalController() || !Target)
	{
		return;
	}
	if (UDamageNumbersSubsystem* const Numbers = GetWorld() ? GetWorld()->GetSubsystem<UDamageNumbersSubsystem>() : nullptr)
	{
		Numbers->ReportDamageNumber(Target, Damage, ArmorDamage, Flags);
	}
}

void AShooterPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// NOTE: IMC setup moved to BeginPlay() to ensure AddMappingContext and
	// RegisterInputMappingContexts happen together (prevents Vector2D corruption)

#if !UE_BUILD_SHIPPING
	// Debug slow motion on F6, for looking at effects frame by frame: 1 -> 0.25 -> 0.05 -> 1.
	// It only turns the existing polarity.debug.timescale; URunDirectorSubsystem applies that to
	// the world on the authority every tick, so on a remote client the key changes nothing.
	// A raw key and not an input action on purpose: a debug key should not need an asset.
	if (InputComponent)
	{
		FInputKeyBinding SlowMotion(FInputChord(EKeys::F6), IE_Pressed);
		SlowMotion.KeyDelegate.GetDelegateForManualSet().BindLambda([]()
		{
			IConsoleVariable* const TimeScale = IConsoleManager::Get().FindConsoleVariable(TEXT("polarity.debug.timescale"));
			if (!TimeScale)
			{
				return;
			}

			const float Current = TimeScale->GetFloat();
			const float Next = (Current > 0.5f) ? 0.25f : (Current > 0.1f ? 0.05f : 1.0f);
			TimeScale->Set(Next, ECVF_SetByConsole);
			UE_LOG(LogTemp, Log, TEXT("[TIME_DEBUG] polarity.debug.timescale = %.2f"), Next);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(7700, 2.0f, FColor::Yellow, FString::Printf(TEXT("Time x%.2f"), Next));
			}
		});
		InputComponent->KeyBindings.Add(SlowMotion);

		// Debug infinite ammo on F7: toggles polarity.debug.infiniteammo (see ShooterWeapon.cpp).
		FInputKeyBinding InfiniteAmmo(FInputChord(EKeys::F7), IE_Pressed);
		InfiniteAmmo.KeyDelegate.GetDelegateForManualSet().BindLambda([]()
		{
			if (IConsoleVariable* const Cheat = IConsoleManager::Get().FindConsoleVariable(TEXT("polarity.debug.infiniteammo")))
			{
				const int32 Next = Cheat->GetInt() != 0 ? 0 : 1;
				Cheat->Set(Next, ECVF_SetByConsole);
				UE_LOG(LogTemp, Log, TEXT("[AMMO_DEBUG] polarity.debug.infiniteammo = %d"), Next);
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(7701, 2.0f, FColor::Yellow,
						Next ? TEXT("Infinite ammo ON") : TEXT("Infinite ammo OFF"));
				}
			}
		});
		InputComponent->KeyBindings.Add(InfiniteAmmo);
	}
#endif
}

namespace
{
	/** Compact description of who this controller is and where it lives, for the coop logs. */
	FString DescribeCoopContext(const APlayerController* PC, const APawn* Pawn)
	{
		const UEnum* RoleEnum = StaticEnum<ENetRole>();
		const auto RoleName = [RoleEnum](ENetRole Role)
		{
			return RoleEnum ? RoleEnum->GetNameStringByValue((int64)Role) : FString::FromInt((int32)Role);
		};

		return FString::Printf(
			TEXT("PC=%s local=%d authority=%d netmode=%d | Pawn=%s class=%s localRole=%s remoteRole=%s"),
			PC ? *PC->GetName() : TEXT("NULL"),
			(PC && PC->IsLocalController()) ? 1 : 0,
			(PC && PC->HasAuthority()) ? 1 : 0,
			PC ? (int32)PC->GetNetMode() : -1,
			Pawn ? *Pawn->GetName() : TEXT("NULL"),
			Pawn ? *Pawn->GetClass()->GetName() : TEXT("NULL"),
			Pawn ? *RoleName(Pawn->GetLocalRole()) : TEXT("-"),
			Pawn ? *RoleName(Pawn->GetRemoteRole()) : TEXT("-"));
	}
}

void AShooterPlayerController::AcknowledgePossession(APawn* InPawn)
{
	Super::AcknowledgePossession(InPawn);

	// Client-side. If a client can see its character but cannot move it, this line and the
	// OnPossess line below will disagree about which pawn it is, or this line will not appear.
	UE_LOG(LogTemp, Verbose, TEXT("[COOP_DEBUG] AcknowledgePossession: %s"),
		*DescribeCoopContext(this, InPawn));

	// This is a client's only chance to hook its HUD up to its pawn: OnPossess never runs here.
	BindToPossessedCharacter(InPawn);
}

void AShooterPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Server-side only. Seeing this twice for the same controller and pawn is the cause of the
	// "InvocationList[CurFunctionIndex] != InDelegate" ensure below: every AddDynamic here would
	// then be bound twice. PossessCount makes the repeat obvious instead of inferred.
	PossessCount++;
	UE_LOG(LogTemp, Verbose, TEXT("[COOP_DEBUG] OnPossess #%d: %s"),
		PossessCount, *DescribeCoopContext(this, InPawn));

	// subscribe to the pawn's OnDestroyed delegate
	InPawn->OnDestroyed.RemoveDynamic(this, &AShooterPlayerController::OnPawnDestroyed);
	InPawn->OnDestroyed.AddDynamic(this, &AShooterPlayerController::OnPawnDestroyed);

	BindToPossessedCharacter(InPawn);
}

void AShooterPlayerController::BindToPossessedCharacter(APawn* InPawn)
{
	// Called from BOTH sides. OnPossess is server only, so a client that never runs this ends up
	// with a HUD subscribed to nothing: no health, no ammo, no weapon, no cooldowns. That is
	// exactly what the first coop session showed, with the client's HP bar frozen at full while
	// hit sounds still played, because those come from the character and never touch the controller.
	//
	// Every binding below is Remove-then-Add so calling this twice is harmless: on a listen server
	// host both OnPossess and AcknowledgePossession fire for the same pawn.
	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(InPawn))
	{
		// add the player tag
		ShooterCharacter->Tags.AddUnique(PlayerPawnTag);

		// The HUD slots (weapon block, crosshair, ability bar) are rebound by the UHudRegistry,
		// which listens to OnPossessedPawnChanged on this controller. Only the overlay is ours.
		// InitializeFor closes it first, so a player who died with the inventory open comes back
		// holding a mouse cursor over a live game.
		if (InventoryScreen)
		{
			InventoryScreen->InitializeFor(ShooterCharacter);
		}
		if (ShopScreen)
		{
			ShopScreen->InitializeFor(ShooterCharacter);
		}

		// force update the life bar + armor bar
		ShooterCharacter->BroadcastHealthChanged();
	}
}

void AShooterPlayerController::OnPawnDestroyed(AActor* DestroyedActor)
{
	if (!HasAuthority() || !GetWorld() || GetWorld()->bIsTearingDown)
	{
		return;
	}
	// Mech parts survive death (author 2026-09-30, Docs/Lane_Camps_Design_2026-09-30.md). The body and
	// its bag die together, so the parts are read off the old bag here and put into the new one below.
	TArray<FInventoryItem> KeptParts;
	if (const AShooterCharacter* const DeadCharacter = Cast<AShooterCharacter>(DestroyedActor))
	{
		if (const UInventoryComponent* const DeadInventory = DeadCharacter->GetInventoryComponent())
		{
			for (const FInventorySlot& Cell : DeadInventory->GetSlots())
			{
				if (Cell.Kind == EInventorySlotKind::MechPart && Cell.Payload)
				{
					FInventoryItem& Kept = KeptParts.AddDefaulted_GetRef();
					Kept.Kind = EInventorySlotKind::MechPart;
					Kept.Payload = Cell.Payload;
				}
			}
		}
	}

	// find the player start
	TArray<AActor*> ActorList;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), ActorList);
	TArray<AActor*> HubStarts;
	for (AActor* Start : ActorList)
	{
		if (Start->ActorHasTag(TEXT("CombatBenchHubSpawn"))) HubStarts.Add(Start);
	}
	const bool bRespawnInHub = !HubStarts.IsEmpty();
	if (bRespawnInHub) ActorList = MoveTemp(HubStarts);

	if (ActorList.Num() > 0)
	{
		// select a random player start
		AActor* RandomPlayerStart = ActorList[FMath::RandRange(0, ActorList.Num() - 1)];

		// spawn a character at the player start
		const FTransform SpawnTransform = RandomPlayerStart->GetActorTransform();

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		TSubclassOf<AShooterCharacter> RespawnClass = CharacterClass;
		if (bRespawnInHub && Cast<AShooterCharacter>(DestroyedActor)) RespawnClass = DestroyedActor->GetClass();
		if (AShooterCharacter* RespawnedCharacter = GetWorld()->SpawnActor<AShooterCharacter>(RespawnClass, SpawnTransform, SpawnParams))
		{
			// possess the character
			Possess(RespawnedCharacter);
			SetControlRotation(SpawnTransform.Rotator());
			ClientSetRotation(SpawnTransform.Rotator(), true);
			if (bRespawnInHub)
			{
				ClientSetViewTarget(RespawnedCharacter);
				ClientSetCameraFade(false);
			}
			UE_LOG(LogTemp, Log, TEXT("[BENCH] respawn %s at %s (%s)"), *GetName(), *GetNameSafe(RandomPlayerStart), *RespawnedCharacter->GetActorLocation().ToString());

			if (UInventoryComponent* const NewInventory = RespawnedCharacter->GetInventoryComponent())
			{
				for (const FInventoryItem& Kept : KeptParts)
				{
					if (NewInventory->TryAdd(Kept) > 0)
					{
						UE_LOG(LogTemp, Warning, TEXT("[CAMP_DEBUG] %s: no cell for kept mech part %s after respawn"),
							*GetName(), *GetNameSafe(Kept.Payload));
					}
				}
				if (KeptParts.Num() > 0)
				{
					UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] %s: %d mech parts carried into the new body"), *GetName(), KeptParts.Num());
				}
			}
		}
	}
}
