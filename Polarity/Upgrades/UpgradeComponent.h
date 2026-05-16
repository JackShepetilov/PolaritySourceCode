// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UpgradeComponent.generated.h"

class UUpgradeDefinition;
class AShooterCharacter;
class AShooterWeapon;

/**
 * Base class for all upgrade logic components.
 * Each upgrade subclass implements its own gameplay logic.
 * Added dynamically to ShooterCharacter by UUpgradeManagerComponent.
 */
UCLASS(Abstract, BlueprintType)
class POLARITY_API UUpgradeComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UUpgradeComponent();

	/** Reference to the definition that spawned this component */
	UPROPERTY(BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UUpgradeDefinition> UpgradeDefinition;

	/** Current level of this upgrade. Starts at 1 on grant, increments via UpgradeManagerComponent::GrantUpgrade. */
	UPROPERTY(BlueprintReadOnly, Category = "Upgrade")
	int32 CurrentLevel = 1;

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	int32 GetCurrentLevel() const { return CurrentLevel; }

	/** Get the owning ShooterCharacter */
	UFUNCTION(BlueprintPure, Category = "Upgrade")
	AShooterCharacter* GetShooterCharacter() const;

	/** Get the owning character's currently equipped weapon */
	UFUNCTION(BlueprintPure, Category = "Upgrade")
	AShooterWeapon* GetCurrentWeapon() const;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	// ==================== Lifecycle Hooks ====================
	// Override these in subclasses. Called by UpgradeManagerComponent.

	/**
	 * Called when the upgrade is granted to the player.
	 * Use this to bind to delegates, set initial state, etc.
	 */
	virtual void OnUpgradeActivated();

	/**
	 * Called when the upgrade is removed from the player.
	 * Use this to unbind from delegates, clean up state, etc.
	 */
	virtual void OnUpgradeDeactivated();

	/**
	 * Called when this upgrade's level is incremented by a repeat grant.
	 * Override in subclasses to apply per-level scaling (e.g. extra dash charges).
	 * NOT called on the very first grant — use OnUpgradeActivated for level-1 setup.
	 * @param OldLevel Level before increment.
	 * @param NewLevel Level after increment (= OldLevel + 1).
	 */
	virtual void OnLevelChanged(int32 OldLevel, int32 NewLevel) {}

	// ==================== Event Hooks ====================
	// Override these in subclasses to react to game events.
	// Called by UpgradeManagerComponent's Notify* methods.

	/** Called when the owner's weapon fires a shot */
	virtual void OnWeaponFired() {}

	/** Called when the owner switches weapons */
	virtual void OnWeaponChanged(AShooterWeapon* OldWeapon, AShooterWeapon* NewWeapon) {}

	/** Called when the owner takes damage */
	virtual void OnOwnerTookDamage(float Damage, AActor* DamageCauser) {}

	/** Called when the owner deals damage to a target */
	virtual void OnOwnerDealtDamage(AActor* Target, float Damage, bool bKilled) {}

	/**
	 * Called whenever the owner's hitscan ionization successfully applies charge to a target.
	 * Independent of damage — fires for 0-damage ionizer hits (wave pistol).
	 * Sourced from AShooterWeapon::ApplyHitscanIonization after shield-block check and charge transfer.
	 */
	virtual void OnHitscanIonized(AActor* Target) {}

	/** Called when owner collects a health pickup while at full HP */
	virtual void OnHealthPickupCollectedAtFullHP() {}

	/**
	 * Return a damage multiplier that this upgrade applies to hitscan shots.
	 * Called by the weapon BEFORE applying damage, so upgrades can scale damage up or down.
	 * @param Target The actor being shot at
	 * @return Multiplier (1.0 = no change, >1.0 = bonus, <1.0 = penalty)
	 */
	virtual float GetDamageMultiplier(AActor* Target) const { return 1.0f; }

	/**
	 * Return a damage multiplier that this upgrade applies to MELEE base damage
	 * (fist swing or melee weapon). Called by MeleeAttackComponent / ShooterWeapon_Melee
	 * before applying TakeDamage. Separate from hitscan so upgrades can target one
	 * or the other (e.g. Backstab only buffs melee).
	 * @param Target The actor being struck
	 * @return Multiplier (1.0 = no change, >1.0 = bonus, <1.0 = penalty)
	 */
	virtual float GetMeleeDamageMultiplier(AActor* Target) const { return 1.0f; }

	/**
	 * Return a multiplier this upgrade applies to MELEE knockback distance on the target.
	 * Called by MeleeAttackComponent just before forwarding the knockback to AShooterNPC::ApplyKnockback.
	 * Multipliers from all active upgrades are combined (multiplicatively).
	 * @param Target The actor being struck
	 * @return Multiplier (1.0 = no change, >1.0 = stronger knockback, <1.0 = weaker)
	 */
	virtual float GetMeleeKnockbackDistanceMultiplier(AActor* Target) const { return 1.0f; }

private:

	/** Cached owner reference */
	TWeakObjectPtr<AShooterCharacter> CachedOwner;

	// UpgradeManagerComponent needs access to lifecycle hooks
	friend class UUpgradeManagerComponent;
};
