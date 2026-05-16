// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_TractorBeam.generated.h"

class UUpgradeDefinition_TractorBeam;
class AShooterNPC;

/**
 * "Tractor Beam" Upgrade
 *
 * Every successful ionization hit (wave pistol or any hitscan with bUseHitscanIonization)
 * yanks the target NPC a short distance toward the player.
 *
 * Levels:
 *  - Lv1: pull stops once target is inside player's capture-handoff range (MinDistanceFromPlayer).
 *  - Lv2: pull works at any range (down to a tiny body-contact buffer). NPCs hit by melee while
 *    actively being pulled receive bonus damage + bonus knockback, and are tagged with
 *    `bKilledByTractorBeamCombo` on death so the kill-attribution code can branch.
 *
 * Easter egg (UpgradeDefinition_TractorBeam::bClassicMode): disables the MinDistance gate
 * entirely — pull always applies, reproducing the original wallslam-everything behaviour.
 *
 * Excluded targets:
 *  - SniperTurretNPC (immune per design spec §5.4)
 *  - Captured NPCs (ApplyKnockback's own guard)
 *  - NPCs stunned by explosion (ApplyKnockback's other guard)
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tractor Beam"))
class POLARITY_API UUpgrade_TractorBeam : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_TractorBeam();

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnHitscanIonized(AActor* Target) override;
	virtual void OnOwnerDealtDamage(AActor* Target, float Damage, bool bKilled) override;
	virtual float GetMeleeDamageMultiplier(AActor* Target) const override;
	virtual float GetMeleeKnockbackDistanceMultiplier(AActor* Target) const override;

private:

	/** Cached pointer to our typed definition */
	TWeakObjectPtr<UUpgradeDefinition_TractorBeam> DefTractorBeam;

	/** Active pulls: NPC → world-time when their combo window expires. Drives bonus damage/knockback gating. */
	TMap<TWeakObjectPtr<AShooterNPC>, double> ActivePullExpiry;

	/** Returns true if the NPC is currently within its combo window (was pulled and the window hasn't expired). */
	bool IsNPCBeingPulled(const AShooterNPC* NPC) const;

	/** Mark NPC as actively being pulled for ComboWindow seconds. */
	void MarkNPCPulled(AShooterNPC* NPC);
};
