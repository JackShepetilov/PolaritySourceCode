// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_PistolStun.generated.h"

class UUpgradeDefinition_PistolStun;
class AShooterNPC;

/**
 * "Pistol Stun" Upgrade
 *
 * Every successful ionization hit (wave pistol or any hitscan with bUseHitscanIonization)
 * applies a short stun to the target NPC, freezing AI and interrupting burst fire.
 *
 * Per-target cooldown prevents stun-lock spam — the same NPC cannot be re-stunned
 * until StunCooldownPerTarget seconds have elapsed since its previous stun.
 *
 * Excluded targets:
 *  - SniperTurretNPC (immune per design spec §5.4)
 *  - HumanoidNPC in melee mode (body is not chargeable per §0.1 / §5.5)
 */
UCLASS(BlueprintType, meta = (DisplayName = "Pistol Stun"))
class POLARITY_API UUpgrade_PistolStun : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_PistolStun();

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnHitscanIonized(AActor* Target) override;

private:

	/** Cached pointer to our typed definition */
	TWeakObjectPtr<UUpgradeDefinition_PistolStun> DefPistolStun;

	/** Per-target last-stun timestamps (world seconds) for cooldown gating. */
	TMap<TWeakObjectPtr<AShooterNPC>, double> LastStunTimeByNPC;
};
