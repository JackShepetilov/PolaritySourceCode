// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RunSubsystem.h"
#include "ShooterPlayerController.generated.h"

class UInputMappingContext;
class AShooterCharacter;
class AShooterWeapon;
class UShooterBulletCounterUI;
class UUpgradeChoiceWidget;
class UAbilityResourceBar;
class UInventoryBarWidget;
class UInventoryScreenWidget;
class UMapScreenWidget;
class UCrosshairWidget;

/** Tells run-HUD widgets to show/hide (e.g. for cutscenes).
 *  bVisible  = appear (true) vs disappear (false).
 *  bAnimated = play the widget's fade animation (true) vs snap instantly (false). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRunHUDVisibilityChanged, bool, bVisible, bool, bAnimated);

/**
 *  Simple PlayerController for a first person shooter game
 *  Manages input mappings
 *  Respawns the player pawn when it's destroyed
 */
UCLASS(abstract)
class POLARITY_API AShooterPlayerController : public APlayerController
{
	GENERATED_BODY()

public:

	AShooterPlayerController();

protected:

	/** Input mapping contexts for this player */
	UPROPERTY(EditAnywhere, Category = "Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category = "Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category = "Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** Character class to respawn when the possessed pawn is destroyed */
	UPROPERTY(EditAnywhere, Category = "Shooter|Respawn")
	TSubclassOf<AShooterCharacter> CharacterClass;

	/** Type of bullet counter UI widget to spawn */
	UPROPERTY(EditAnywhere, Category = "Shooter|UI")
	TSubclassOf<UShooterBulletCounterUI> BulletCounterUIClass;

	/** Tag to grant the possessed pawn to flag it as the player */
	UPROPERTY(EditAnywhere, Category = "Shooter|Player")
	FName PlayerPawnTag = FName("Player");

	/** Pointer to the bullet counter UI widget */
	TObjectPtr<UShooterBulletCounterUI> BulletCounterUI;

	/** Type of ability/resource bar widget to spawn (WBP_AbilityResourceBar). */
	UPROPERTY(EditAnywhere, Category = "Shooter|UI")
	TSubclassOf<UAbilityResourceBar> AbilityResourceBarClass;

	/** Pointer to the ability/resource bar widget. */
	UPROPERTY(Transient)
	TObjectPtr<UAbilityResourceBar> AbilityResourceBar;

	/** Type of corner weapon block to spawn (WBP_InventoryBar): the two weapon rows. */
	UPROPERTY(EditAnywhere, Category = "Shooter|UI")
	TSubclassOf<UInventoryBarWidget> InventoryBarClass;

	/** Pointer to the corner weapon block. */
	UPROPERTY(Transient)
	TObjectPtr<UInventoryBarWidget> InventoryBar;

	/** Type of inventory overlay to spawn (WBP_InventoryScreen): the cell grid behind a key. */
	UPROPERTY(EditAnywhere, Category = "Shooter|UI")
	TSubclassOf<UInventoryScreenWidget> InventoryScreenClass;

	/** Pointer to the inventory overlay. Created hidden and toggled by ToggleInventoryScreen. */
	UPROPERTY(Transient)
	TObjectPtr<UInventoryScreenWidget> InventoryScreen;

	/** Type of map screen to spawn. Leave unset and the map key does nothing; set it to a Blueprint
	 *  subclass of UMapScreenWidget to restyle without touching code. */
	UPROPERTY(EditAnywhere, Category = "Shooter|UI")
	TSubclassOf<UMapScreenWidget> MapScreenClass;

	/** The map screen. Created hidden at startup rather than on demand, so the first press is as
	 *  fast as every later one. */
	UPROPERTY(Transient)
	TObjectPtr<UMapScreenWidget> MapScreen;

	/** Type of crosshair widget to spawn (WBP_Crosshair). If unset, no crosshair is shown. */
	UPROPERTY(EditAnywhere, Category = "Shooter|UI")
	TSubclassOf<UCrosshairWidget> CrosshairWidgetClass;

	/** Pointer to the crosshair widget. */
	UPROPERTY(Transient)
	TObjectPtr<UCrosshairWidget> CrosshairWidget;

	// ==================== Roguelite Run Widgets ====================

	/**
	 * Run-HUD container widget — instantiated when a run starts.
	 * Typically WBP_RunHUD that statically embeds 4 instances of WBP_XPBar
	 * (one per ESkillCategory).
	 */
	UPROPERTY(EditAnywhere, Category = "Shooter|UI|Run")
	TSubclassOf<UUserWidget> XPBarWidgetClass;

	/** Modal upgrade choice widget class — instantiated when a run starts, opens on level-up. */
	UPROPERTY(EditAnywhere, Category = "Shooter|UI|Run")
	TSubclassOf<UUpgradeChoiceWidget> UpgradeChoiceWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> XPBarWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUpgradeChoiceWidget> UpgradeChoiceWidget;

protected:

	/** Gameplay Initialization */
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Initialize input bindings */
	virtual void SetupInputComponent() override;

	/** Pawn initialization. Server only: clients never get this call. */
	virtual void OnPossess(APawn* InPawn) override;

	/** Client-side counterpart of OnPossess: this is where a client learns which pawn it drives.
	 *  Also the client's only chance to bind its HUD, since OnPossess never runs on a client. */
	virtual void AcknowledgePossession(APawn* InPawn) override;

	/** Subscribe this controller's HUD to the pawn it drives. Safe to call more than once, and
	 *  called from both OnPossess (server) and AcknowledgePossession (client). */
	void BindToPossessedCharacter(APawn* InPawn);

	/** How many times this controller has possessed anything. Diagnostic for the duplicate
	 *  OnPossess that makes the delegate AddDynamic calls fire an ensure. */
	int32 PossessCount = 0;

	/** Called if the possessed pawn is destroyed */
	UFUNCTION()
	void OnPawnDestroyed(AActor* DestroyedActor);

	/** Called when the bullet count on the possessed pawn is updated */
	UFUNCTION()
	void OnBulletCountUpdated(int32 MagazineSize, int32 Bullets);

	/** Called when the possessed pawn health/armor snapshot changes */
	UFUNCTION()
	void OnPawnHealthChanged(float CurrentHP, float MaxHP, float LifePercent, float ArmorPercent);

	/** Called when damage is received from a direction */
	UFUNCTION()
	void OnDamageDirection(float AngleDegrees, float Damage);

	/** Called when the weapon heat level is updated */
	UFUNCTION()
	void OnHeatUpdated(float HeatPercent, float DamageMultiplier);

	/** Called when the pawn speed is updated */
	UFUNCTION()
	void OnSpeedUpdated(float SpeedPercent, float CurrentSpeed, float MaxSpeed);

	/** Called when the charge polarity changes */
	UFUNCTION()
	void OnPolarityChanged(uint8 NewPolarity, float ChargeValue);

	/** Called every tick with current charge value */
	UFUNCTION()
	void OnChargeUpdated(float ChargeValue, uint8 Polarity);

	/** Called when drop kick cooldown starts */
	UFUNCTION()
	void OnDropKickCooldownStarted(float CooldownDuration);

	/** Called when drop kick cooldown ends */
	UFUNCTION()
	void OnDropKickCooldownEnded();

	/** Called when melee weapon is equipped or unequipped */
	UFUNCTION()
	void OnMeleeWeaponEquipped(bool bEquipped, int32 RemainingHits, int32 MaxHits);

	/** Called when the active (held) weapon changes; forwards it to the crosshair (nullptr = unarmed). */
	UFUNCTION()
	void OnActiveWeaponChanged(AShooterWeapon* NewWeapon);

	// ==================== Run lifecycle handlers ====================

	UFUNCTION()
	void HandleRunStarted();

	UFUNCTION()
	void HandleRunEnded(ERunEndReason Reason);

	void CreateRunWidgets();
	void DestroyRunWidgets();

public:

	// ==================== Inventory overlay ====================

	/** Show or hide the inventory overlay. Purely local: the screen is UI, it sends nothing to the
	 *  server, and a remote controller has no viewport to put it in. */
	UFUNCTION(BlueprintCallable, Category = "Shooter|UI")
	void ToggleInventoryScreen();

	/** Hide the inventory overlay if it is open. Called when the pawn dies, so input never stays
	 *  in cursor mode with a corpse underneath it. */
	UFUNCTION(BlueprintCallable, Category = "Shooter|UI")
	void CloseInventoryScreen();

	UFUNCTION(BlueprintPure, Category = "Shooter|UI")
	bool IsInventoryScreenOpen() const;

	/** Show or hide the map. Local only, same as the inventory: it is a picture of state the client
	 *  already has, and it sends nothing anywhere. */
	UFUNCTION(BlueprintCallable, Category = "Shooter|UI")
	void ToggleMapScreen();

	UFUNCTION(BlueprintCallable, Category = "Shooter|UI")
	void CloseMapScreen();

	UFUNCTION(BlueprintPure, Category = "Shooter|UI")
	bool IsMapScreenOpen() const;

	// ==================== Squad spawn console commands ====================
	// First Exec functions in the project; they route into USquadSpawnSubsystem.
	// Usage:  SquadSpawn <PointTag> <LoadoutPath> | SquadRunScenario <ScenarioPath> | SquadClear

	/** Spawn a squad at every spawn point with the given tag */
	UFUNCTION(Exec, Category = "Squad")
	void SquadSpawn(const FString& PointTag, const FString& LoadoutPath);

	/** Run a whole scenario (pairs of point tag -> loadout) */
	UFUNCTION(Exec, Category = "Squad")
	void SquadRunScenario(const FString& ScenarioPath);

	/** Destroy everything the squad system spawned */
	UFUNCTION(Exec, Category = "Squad")
	void SquadClear();

	// ==================== HUD cutscene visibility ====================

	/** Run-HUD widgets (HP/charge counter, ability bar, …) bind to this to show/hide for cutscenes. */
	UPROPERTY(BlueprintAssignable, Category = "Shooter|UI")
	FOnRunHUDVisibilityChanged OnRunHUDVisibilityChanged;

	/** Broadcast OnRunHUDVisibilityChanged. Call from a cutscene: SetRunHUDVisible(false, bAnimated) on
	 *  enter, SetRunHUDVisible(true, bAnimated) on exit. bAnimated = fade animation vs instant snap. */
	UFUNCTION(BlueprintCallable, Category = "Shooter|UI")
	void SetRunHUDVisible(bool bVisible, bool bAnimated);
};
