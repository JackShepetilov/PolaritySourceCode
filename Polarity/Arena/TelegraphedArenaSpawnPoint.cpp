// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "TelegraphedArenaSpawnPoint.h"

void ATelegraphedArenaSpawnPoint::OnSpawnTelegraphed(TSubclassOf<AShooterNPC> NPCClass, float Duration)
{
	OnTelegraphStarted.Broadcast(NPCClass, Duration);
}
