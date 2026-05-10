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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUpgradeGranted, UUpgradeDefinition*, Definition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUpgradeRemoved, UUpgradeDefinition*, Definition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnUpgradeLeveledUp, UUpgradeDefinition*, Definition, int32, NewLevel);

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
	 * Grant an upgrade to the player.
	 *  - If not yet owned: creates the upgrade component at CurrentLevel=1 and calls OnUpgradeActivated.
	 *  - If already owned and below MaxLevel: increments CurrentLevel and calls OnLevelChanged.
	 *  - If already at MaxLevel: returns false (no-op).
	 * @return True if upgrade was newly granted OR levelled up.
	 */
	UFUNCTION(BlueprintCallable, Category = "Upgrades")
	bool GrantUpgrade(UUpgradeDefinition* Definition);

	/**
	 * Remove an upgrade from the player.
	 * @return True if upgrade was removed
	 */
	UFUNCTION(BlueprintCallable, Category = "Upgrades")
	bool RemoveUpgrade(FGameplayTag UpgradeTag);

	/** Check if player has a specific upgrade */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	bool HasUpgrade(FGameplayTag UpgradeTag) const;

	/** Current level for an owned upgrade (1+). Returns 0 if not owned. */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	int32 GetUpgradeLevel(FGameplayTag UpgradeTag) const;

	/** True if upgrade is owned AND CurrentLevel >= Definition->MaxLevel. False if not owned (still grantable). */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	bool IsUpgradeMaxedOut(UUpgradeDefinition* Definition) const;

	/** Get all acquired upgrade definitions (for UI) */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	TArray<UUpgradeDefinition*> GetAcquiredUpgrades() const;

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

	/** Broadcast when an already-owned upgrade is levelled up by a repeat grant. */
	UPROPERTY(BlueprintAssignable, Category = "Upgrades")
	FOnUpgradeLeveledUp OnUpgradeLeveledUp;

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

	/** Called when owner collects a health pickup while at full HP */
	void NotifyHealthPickupCollectedAtFullHP();

	/** Query all active upgrades for their combined damage multiplier against a target */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	float GetCombinedDamageMultiplier(AActor* Target) const;

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
