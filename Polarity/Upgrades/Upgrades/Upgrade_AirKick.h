// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_AirKick.generated.h"

class UUpgradeDefinition_AirKick;
class UDamageType;

/**
 * "Air Mail" Upgrade (class kept as AirKick for asset/registry compatibility).
 *
 * BOUNCE: thrown yanked weapons, launched props and launched NPCs that hit an enemy or a
 * surface at a near-perpendicular angle (60–120° incidence band) — and didn't explode /
 * survived the impact — bounce off and fly to a point at the player's head height. The
 * impact hooks live in the objects' own hit handlers (ADroppedRangedWeapon::OnWeaponMeshHit,
 * AEMFPhysicsProp::OnPropHit/OnPropOverlap, AShooterNPC::OnCapsuleHit); they consult this
 * upgrade through the static helpers below and mark the state with actor tags:
 *   TAG_AirMailIncoming — bounced, flying to the player, can be kicked;
 *   TAG_AirMailKicked   — air-melee'd, flying forward, carries KickDamage.
 *
 * KICK: if the player melee-hits an incoming object while airborne, HandleMeleeHit redirects
 * it along the camera forward at KickSpeed; on its next impact the object deals KickDamage
 * (a kicked NPC projectile damages both itself and its target).
 */
UCLASS(BlueprintType, meta = (DisplayName = "Air Mail"))
class POLARITY_API UUpgrade_AirKick : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_AirKick();

	/** Actor tag: object bounced off a surface and is flying toward the player. */
	static const FName TAG_AirMailIncoming;

	/** Actor tag: object was kicked by the player and carries KickDamage. */
	static const FName TAG_AirMailKicked;

	/** Find the local player's Air Mail upgrade component (nullptr if not owned). */
	static UUpgrade_AirKick* FindActiveAirMail(const UObject* WorldContextObject);

	/** True if the impact qualifies for the bounce: pre-impact speed above the threshold and
	 *  incidence angle to the surface plane >= MinBounceAngleDeg (i.e. within the 60–120° band,
	 *  glancing/sliding hits rejected). */
	bool QualifiesForBounce(const FVector& PreImpactVelocity, const FVector& ImpactNormal) const;

	/** Compute the return velocity from FromLocation to a point at the player's head height
	 *  (+ReturnTargetHeightOffset), with simple gravity compensation for the flight time.
	 *  Returns false if the player/definition is unavailable. */
	bool ComputeReturnVelocity(const FVector& FromLocation, FVector& OutVelocity) const;

	/** QualifiesForBounce + ComputeReturnVelocity in one call. */
	bool TryComputeBounce(const FVector& FromLocation, const FVector& PreImpactVelocity,
		const FVector& ImpactNormal, FVector& OutVelocity) const;

	/** Spawn the optional bounce FX/sound at the bounce point. */
	void PlayBounceFeedback(const FVector& Location) const;

	/** Damage carried by a kicked object (0 if definition missing). */
	float GetKickDamage() const;

	/** Damage type for the kick damage (falls back to UDamageType_Melee when unset). */
	TSubclassOf<UDamageType> GetKickDamageType() const;

	/** Spin speed (deg/s) for bounced weapons/props. */
	float GetReturnSpinSpeed() const;

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;

private:

	/** Cached pointer to our typed definition */
	TWeakObjectPtr<UUpgradeDefinition_AirKick> DefAirKick;

	/** Bound to MeleeAttackComponent->OnMeleeHit: redirects TAG_AirMailIncoming objects. */
	UFUNCTION()
	void HandleMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage);

	/** Kick feedback (FX + sound) at the contact point. */
	void PlayKickFeedback(const FVector& Location, const FVector& Direction) const;
};
