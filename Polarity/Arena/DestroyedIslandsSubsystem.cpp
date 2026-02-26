// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "DestroyedIslandsSubsystem.h"

void UDestroyedIslandsSubsystem::RegisterDestroyedIsland(FName IslandID)
{
	if (!IslandID.IsNone())
	{
		DestroyedIslandIDs.Add(IslandID);
	}
}

bool UDestroyedIslandsSubsystem::IsIslandDestroyed(FName IslandID) const
{
	return DestroyedIslandIDs.Contains(IslandID);
}

void UDestroyedIslandsSubsystem::ClearDestroyedIslands()
{
	DestroyedIslandIDs.Empty();
}
