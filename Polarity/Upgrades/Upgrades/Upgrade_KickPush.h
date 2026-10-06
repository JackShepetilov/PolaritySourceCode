// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_KickPush.generated.h"

class UMeleeAttackComponent;

/**
 * A kick (the swing made in the air) that lands throws the enemy further and kicks the owner back
 * the other way along the same line. @see UUpgradeDefinition_KickPush
 *
 * The enemy's half rides the existing knockback multiplier, so it reaches the server the way every
 * shove already does. The owner's half is launched on the machine that swung and reported up.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Kick Push"))
class POLARITY_API UUpgrade_KickPush : public UUpgradeComponent
{
	GENERATED_BODY()

protected:
	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
	virtual float GetMeleeKnockbackDistanceMultiplier(AActor* Target) const override;

private:
	TWeakObjectPtr<UMeleeAttackComponent> CachedMeleeComp;

	/** True when the swing in progress is a kick. */
	bool IsKicking() const;

	UFUNCTION()
	void HandleMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage);
};
