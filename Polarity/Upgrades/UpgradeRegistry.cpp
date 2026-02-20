// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "UpgradeRegistry.h"
#include "UpgradeDefinition.h"

UUpgradeDefinition* UUpgradeRegistry::FindByTag(FGameplayTag Tag) const
{
	for (const TObjectPtr<UUpgradeDefinition>& Def : AllUpgrades)
	{
		if (Def && Def->UpgradeTag == Tag)
		{
			return Def;
		}
	}
	return nullptr;
}
