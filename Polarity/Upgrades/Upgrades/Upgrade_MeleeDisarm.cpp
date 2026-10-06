// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_MeleeDisarm.h"
#include "UpgradeDefinition_MeleeKit.h"
#include "Variant_Shooter/AI/ShooterNPC.h"

void UUpgrade_MeleeDisarm::OnMeleeHitAuthority(AActor* Target)
{
	const UUpgradeDefinition_MeleeDisarm* const Def = Cast<UUpgradeDefinition_MeleeDisarm>(UpgradeDefinition);
	if (!Def)
	{
		return;
	}

	if (AShooterNPC* const Enemy = Cast<AShooterNPC>(Target))
	{
		Enemy->ApplyFireLock(Def->DisarmDuration);
	}
}
