// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_ChargeFlip.generated.h"

class UUpgradeDefinition_ChargeFlip;
class AEMFProjectile;

/**
 * "Charge Flip" Upgrade
 *
 * When the player shoots an in-flight EMF projectile with a hitscan rifle,
 * the projectile explodes and fires multiplied-damage rifle shots with ionization
 * at ALL targets visible from the explosion point.
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

private:

	/** Cached typed definition */
	TWeakObjectPtr<UUpgradeDefinition_ChargeFlip> DefCF;

	/**
	 * Trigger the Charge Flip explosion at the given projectile's location.
	 * @param Projectile - The EMF projectile to detonate
	 * @param ChainDepth - Current chain depth (for recursion limit)
	 * @param AlreadyDetonated - Set of projectiles already detonated (prevents loops)
	 */
	void TriggerChargeFlip(AEMFProjectile* Projectile, int32 ChainDepth, TSet<AEMFProjectile*>& AlreadyDetonated);

	/** Apply ionization charge to a target actor */
	void ApplyIonization(AActor* Target);

	/** Spawn beam VFX from Start to End */
	void SpawnBeamEffect(const FVector& Start, const FVector& End);
};
