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

	/** Called when owner collects a health pickup while at full HP */
	virtual void OnHealthPickupCollectedAtFullHP() {}

	/**
	 * Return a damage multiplier that this upgrade applies to hitscan shots.
	 * Called by the weapon BEFORE applying damage, so upgrades can scale damage up or down.
	 * @param Target The actor being shot at
	 * @return Multiplier (1.0 = no change, >1.0 = bonus, <1.0 = penalty)
	 */
	virtual float GetDamageMultiplier(AActor* Target) const { return 1.0f; }

private:

	/** Cached owner reference */
	TWeakObjectPtr<AShooterCharacter> CachedOwner;

	// UpgradeManagerComponent needs access to lifecycle hooks
	friend class UUpgradeManagerComponent;
};
