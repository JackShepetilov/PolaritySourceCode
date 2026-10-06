// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_MeleeDisarm.generated.h"

/**
 * A melee hit leaves the enemy unable to shoot for a while. Applied on the authority, where the
 * enemy lives. @see UUpgradeDefinition_MeleeDisarm, AShooterNPC::ApplyFireLock
 */
UCLASS(BlueprintType, meta = (DisplayName = "Melee Disarm"))
class POLARITY_API UUpgrade_MeleeDisarm : public UUpgradeComponent
{
	GENERATED_BODY()

protected:
	virtual void OnMeleeHitAuthority(AActor* Target) override;
};
