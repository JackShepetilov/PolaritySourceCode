// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_DropKick.generated.h"

/**
 * "Drop Kick" Upgrade — pure unlock.
 *
 * On activation: sets MeleeAttackComponent::bDropKickUnlocked to true.
 * On deactivation: sets it back to false.
 *
 * The actual kick logic, cooldown, cone parameters, damage scaling and charge
 * cost live on the owner's MeleeAttackComponent (FMeleeAttackSettings::DropKick*).
 */
UCLASS(BlueprintType, meta = (DisplayName = "Drop Kick"))
class POLARITY_API UUpgrade_DropKick : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_DropKick();

protected:

	virtual void OnUpgradeActivated() override;
	virtual void OnUpgradeDeactivated() override;
};
