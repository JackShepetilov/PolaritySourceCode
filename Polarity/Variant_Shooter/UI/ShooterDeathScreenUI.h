// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterDeathScreenUI.generated.h"

/**
 * Death Screen UI widget for the shooter game.
 * Shown when player dies, provides respawn/restart options.
 */
UCLASS(abstract)
class POLARITY_API UShooterDeathScreenUI : public UUserWidget
{
	GENERATED_BODY()

public:

	// ==================== Death State ====================

	/**
	 * Called when the death screen is shown.
	 * @param KillerName - name of the entity that killed the player (can be empty)
	 * @param DeathMessage - localized death message
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|DeathScreen", meta = (DisplayName = "OnDeathScreenShown"))
	void BP_OnDeathScreenShown(const FString& KillerName, const FString& DeathMessage);

	/**
	 * Updates respawn timer display.
	 * @param TimeRemaining - seconds until respawn is available
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|DeathScreen", meta = (DisplayName = "UpdateRespawnTimer"))
	void BP_UpdateRespawnTimer(float TimeRemaining);

	/**
	 * Called when respawn becomes available.
	 * Use this to enable the respawn button.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|DeathScreen", meta = (DisplayName = "OnRespawnAvailable"))
	void BP_OnRespawnAvailable();

	// ==================== Stats Display ====================

	/**
	 * Updates session stats on death screen.
	 * @param Kills - total kills this session
	 * @param Deaths - total deaths this session
	 * @param TimeAlive - time alive in seconds before death
	 * @param DamageDealt - total damage dealt this life
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|DeathScreen|Stats", meta = (DisplayName = "UpdateDeathStats"))
	void BP_UpdateDeathStats(int32 Kills, int32 Deaths, float TimeAlive, float DamageDealt);

	// ==================== Actions (called from Blueprint) ====================

	/** Respawn at last checkpoint */
	UFUNCTION(BlueprintCallable, Category = "Shooter|DeathScreen")
	void Respawn();

	/** Respawn at level start */
	UFUNCTION(BlueprintCallable, Category = "Shooter|DeathScreen")
	void RespawnAtStart();

	/** Restart the current level */
	UFUNCTION(BlueprintCallable, Category = "Shooter|DeathScreen")
	void RestartLevel();

	/** Return to main menu */
	UFUNCTION(BlueprintCallable, Category = "Shooter|DeathScreen")
	void ReturnToMainMenu();

	// ==================== Delegates for GameMode ====================

	/** Delegate called when player requests respawn */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRespawnRequestedDelegate);

	UPROPERTY(BlueprintAssignable, Category = "Shooter|DeathScreen")
	FOnRespawnRequestedDelegate OnRespawnRequested;

	/** Delegate called when player requests respawn at start */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRespawnAtStartRequestedDelegate);

	UPROPERTY(BlueprintAssignable, Category = "Shooter|DeathScreen")
	FOnRespawnAtStartRequestedDelegate OnRespawnAtStartRequested;
};
