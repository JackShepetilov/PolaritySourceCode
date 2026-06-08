// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/HitResult.h"
#include "EnemyBeamBoltSubsystem.generated.h"

class AShooterWeapon;

/**
 * One in-flight enemy "bolt": a moving damage region travelling along a frozen beam line.
 * Spawned by AShooterWeapon::PerformSimpleHitscan when an enemy fires a hitscan at a player who
 * has the Low-Health Defense upgrade active. The bolt's leading edge advances at RandSpeed; it
 * only damages the victim if their CURRENT position is still within HitRadius of the line when the
 * window [Front - BeamLength, Front] covers their projected distance — so the player can dodge by
 * stepping off the line before the bolt arrives. All references are weak (no GC keep needed).
 */
struct FEnemyBeamBolt
{
	TWeakObjectPtr<AShooterWeapon> Weapon;
	TWeakObjectPtr<AActor> Victim;
	FVector Start = FVector::ZeroVector;
	FVector Dir = FVector::ForwardVector;
	float MaxDist = 0.0f;
	float RandSpeed = 1.0f;
	float BeamLength = 500.0f;
	float HitRadius = 80.0f;
	float EnergyMultiplier = 1.0f;
	float Age = 0.0f;
};

/**
 * Ticks all active enemy bolts (the deferred, dodgeable hitscan shots produced by the Low-Health
 * Defense upgrade). Centralised in a world subsystem so a bolt outlives the firing weapon's frame
 * and we avoid spawning a per-shot actor. Only ticks while bolts are in flight.
 */
UCLASS()
class POLARITY_API UEnemyBeamBoltSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:

	/** Register a new travelling bolt down an aim line. Damage is applied to Victim only if/when the
	 *  moving window reaches their current position within HitRadius. See FEnemyBeamBolt. */
	void RegisterBolt(AShooterWeapon* Weapon, AActor* Victim,
		const FVector& Start, const FVector& Dir, float MaxDist, float RandSpeed,
		float BeamLength, float HitRadius, float EnergyMultiplier);

	// UTickableWorldSubsystem interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return ActiveBolts.Num() > 0; }

private:

	TArray<FEnemyBeamBolt> ActiveBolts;
};
