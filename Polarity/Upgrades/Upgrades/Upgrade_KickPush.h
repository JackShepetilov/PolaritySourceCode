// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_KickPush.generated.h"

class UMeleeAttackComponent;

/**
 * A kick (the swing made in the air) that lands knocks the owner back the way it knocks the enemy
 * back, the other way. @see UUpgradeDefinition_KickPush
 *
 * It owns no shove of its own. It answers one question, and UMeleeAttackComponent does the rest
 * with the numbers it has already worked out for the enemy.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Kick Push"))
class POLARITY_API UUpgrade_KickPush : public UUpgradeComponent
{
	GENERATED_BODY()

protected:
	virtual void OnUpgradeActivated() override;
	virtual float GetMeleeSelfKnockbackScale(AActor* Target) const override;

private:
	TWeakObjectPtr<UMeleeAttackComponent> CachedMeleeComp;
};
