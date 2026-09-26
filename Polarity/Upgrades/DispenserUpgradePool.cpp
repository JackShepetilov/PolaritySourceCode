// DispenserUpgradePool.cpp

#include "DispenserUpgradePool.h"

#include "UpgradeDefinition.h"

int32 UDispenserUpgradePool::FindSlotOf(const UUpgradeDefinition* Upgrade) const
{
	if (!Upgrade)
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		for (const FDispenserUpgradeEntry& Entry : Slots[Index].Upgrades)
		{
			if (Entry.Upgrade.Get() == Upgrade)
			{
				return Index;
			}
		}
	}
	return INDEX_NONE;
}
