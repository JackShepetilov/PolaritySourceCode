// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArenaSpawnPoint.h"
#include "TelegraphedArenaSpawnPoint.generated.h"

class AShooterNPC;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSpawnTelegraphStarted,
	TSubclassOf<AShooterNPC>, NPCClass, float, Duration);

/**
 * Spawn point that delays its spawn after ArenaManager requests one, broadcasting a
 * delegate so Blueprints can play VFX, flash the floor, or whatever telegraph cue fits.
 *
 * During the delay, ArenaManager treats this point as reserved and will not pick it
 * for any other spawn, so we never double-up on the same telegraphed location.
 */
UCLASS(Blueprintable)
class POLARITY_API ATelegraphedArenaSpawnPoint : public AArenaSpawnPoint
{
	GENERATED_BODY()

public:
	/** Seconds between request and actual NPC appearance — visual cue plays during this window */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telegraph", meta = (ClampMin = "0.1"))
	float TelegraphDelay = 1.5f;

	/** Fired the moment the arena schedules a spawn at this point.
	 *  Use it in BP to spawn a Niagara VFX, flash a decal, play a SFX, etc. */
	UPROPERTY(BlueprintAssignable, Category = "Telegraph")
	FOnSpawnTelegraphStarted OnTelegraphStarted;

	virtual float GetTelegraphDelay() const override { return TelegraphDelay; }
	virtual void OnSpawnTelegraphed(TSubclassOf<AShooterNPC> NPCClass, float Duration) override;
};
