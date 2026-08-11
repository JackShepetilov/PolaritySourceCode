// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "UObject/ObjectKey.h"
#include "Upgrade_ChargeFlip.generated.h"

class UUpgradeDefinition_ChargeFlip;
class AEMFProjectile;
class AEMFPhysicsProp;

/**
 * "Charge Flip" Upgrade
 *
 * When the player shoots an in-flight EMF projectile, or detonates an explosive
 * EMF prop with a hitscan rifle, the explosion fires multiplied-damage rifle
 * shots with ionization at ALL targets visible from the explosion point.
 *
 * If other EMF projectiles are visible from the explosion, they are also hit,
 * triggering a chain reaction.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Charge Flip"))
class POLARITY_API UUpgrade_ChargeFlip : public UUpgradeComponent
{
	GENERATED_BODY()

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnWeaponFired() override;
	virtual void OnWeaponDealtDamage(AShooterWeapon* Weapon, AActor* Target, float Damage, bool bKilled) override;

private:

	/** Cached typed definition */
	TWeakObjectPtr<UUpgradeDefinition_ChargeFlip> DefCF;

	/** Props that already spawned a Charge Flip burst from their death. */
	TSet<TObjectKey<AEMFPhysicsProp>> TriggeredPropFlips;

	/**
	 * Trigger the Charge Flip explosion at the given projectile's location.
	 * @param Projectile - The EMF projectile to detonate
	 * @param ChainDepth - Current chain depth (for recursion limit)
	 * @param AlreadyDetonated - Set of projectiles already detonated (prevents loops)
	 */
	void TriggerChargeFlip(AEMFProjectile* Projectile, int32 ChainDepth, TSet<AEMFProjectile*>& AlreadyDetonated);

	/** Trigger the Charge Flip burst from an already-resolved explosion source. */
	void TriggerChargeFlipAtLocation(const FVector& ExplosionOrigin, AActor* SourceActor, int32 ChainDepth, TSet<AEMFProjectile*>& AlreadyDetonated);

	/** Apply ionization charge to a target actor */
	void ApplyIonization(AActor* Target);

	/** Spawn beam VFX from Start to End */
	void SpawnBeamEffect(const FVector& Start, const FVector& End);
};
