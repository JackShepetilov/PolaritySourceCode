// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_AirKick.generated.h"

class UUpgradeDefinition_AirKick;

/**
 * "Air Kick" Upgrade
 *
 * When the player is airborne and melee-hits an airborne EMFPhysicsProp,
 * the prop is instantly "captured" and "launched" without any channeling animation:
 *   1. Player's polarity toggles (as in reverse channeling)
 *   2. Prop is kicked in the camera's forward direction at high speed
 *   3. Prop deals collision damage on impact (speed-based, same as reverse launch)
 *
 * Both player and prop must be airborne for the effect to trigger.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Air Kick"))
class POLARITY_API UUpgrade_AirKick : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_AirKick();

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;

private:

	/** Cached pointer to our typed definition */
	TWeakObjectPtr<UUpgradeDefinition_AirKick> DefAirKick;

	/** Bound to MeleeAttackComponent->OnMeleeHit */
	UFUNCTION()
	void HandleMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage);

	/** Check if the prop has no ground within trace distance below it */
	bool IsPropAirborne(AActor* Prop) const;
};
