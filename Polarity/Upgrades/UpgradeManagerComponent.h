// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "UpgradeManagerComponent.generated.h"

class UUpgradeDefinition;
class UUpgradeComponent;
class UUpgradeRegistry;
class AShooterWeapon;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUpgradeGranted, const UUpgradeDefinition*, Definition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUpgradeRemoved, const UUpgradeDefinition*, Definition);

/**
 * Manages all active upgrades on the owning ShooterCharacter.
 * Handles granting, removing, querying, and persistence of upgrades.
 */
UCLASS(BlueprintType, meta = (BlueprintSpawnableComponent))
class POLARITY_API UUpgradeManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UUpgradeManagerComponent();

	// ==================== Core API ====================

	/**
	 * Grant an upgrade to the player. Does nothing if already owned.
	 * Creates the upgrade component and activates it.
	 * @return True if upgrade was newly granted
	 */
	UFUNCTION(BlueprintCallable, Category = "Upgrades")
	bool GrantUpgrade(const UUpgradeDefinition* Definition);

	/**
	 * Remove an upgrade from the player.
	 * @return True if upgrade was removed
	 */
	UFUNCTION(BlueprintCallable, Category = "Upgrades")
	bool RemoveUpgrade(FGameplayTag UpgradeTag);

	/** Check if player has a specific upgrade */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	bool HasUpgrade(FGameplayTag UpgradeTag) const;

	/** Get all acquired upgrade definitions (for UI) */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	TArray<const UUpgradeDefinition*> GetAcquiredUpgrades() const;

	/** Get the active component for a specific upgrade (nullptr if not owned) */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	UUpgradeComponent* GetUpgradeComponent(FGameplayTag UpgradeTag) const;

	// ==================== Persistence ====================

	/** Get list of upgrade tags for checkpoint/save serialization */
	TArray<FGameplayTag> GetUpgradeTagsForSave() const;

	/** Restore upgrades from saved tags (used by checkpoint/save system) */
	void RestoreUpgradesFromTags(const TArray<FGameplayTag>& Tags, const UUpgradeRegistry* Registry);

	// ==================== Delegates ====================

	/** Broadcast when an upgrade is granted */
	UPROPERTY(BlueprintAssignable, Category = "Upgrades")
	FOnUpgradeGranted OnUpgradeGranted;

	/** Broadcast when an upgrade is removed */
	UPROPERTY(BlueprintAssignable, Category = "Upgrades")
	FOnUpgradeRemoved OnUpgradeRemoved;

	// ==================== Event Broadcasting ====================
	// Called by core systems (ShooterCharacter, ShooterWeapon).
	// Routes events to all active upgrade components.

	/** Called by ShooterWeapon after a shot is fired */
	void NotifyWeaponFired();

	/** Called by ShooterCharacter when weapon is switched */
	void NotifyWeaponChanged(AShooterWeapon* OldWeapon, AShooterWeapon* NewWeapon);

	/** Called by ShooterCharacter when taking damage */
	void NotifyOwnerTookDamage(float Damage, AActor* DamageCauser);

	/** Called when owner deals damage to a target */
	void NotifyOwnerDealtDamage(AActor* Target, float Damage, bool bKilled);

protected:

	virtual void BeginPlay() override;

private:

	/** Map of UpgradeTag -> active upgrade component */
	UPROPERTY()
	TMap<FGameplayTag, TObjectPtr<UUpgradeComponent>> ActiveUpgrades;

	/** Currently bound weapon (for delegate cleanup) */
	UPROPERTY()
	TWeakObjectPtr<AShooterWeapon> BoundWeapon;

	/** Bind/unbind OnShotFired delegate on weapon */
	void BindToWeapon(AShooterWeapon* Weapon);
	void UnbindFromWeapon();

	/** Callback for weapon's OnShotFired delegate */
	UFUNCTION()
	void OnWeaponShotFiredCallback();
};
