// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/HitResult.h"
#include "EnemyBeamBoltSubsystem.generated.h"

class AShooterWeapon;
class UNiagaraComponent;

/**
 * One in-flight "bolt": a moving damage region travelling along a frozen beam line.
 *
 * Originally enemy-only (an enemy hitscan aimed at a player, dodgeable while the Low-Health Defense
 * upgrade slows it down), and now also what a shotgun pellet is: the same shape of thing, a hit
 * that was decided when the trigger was pulled but only lands when it gets there.
 *
 * The bolt's leading edge advances at RandSpeed; it only damages the victim if their CURRENT
 * position is still within HitRadius of the line when the window [Front - BeamLength, Front] covers
 * their projected distance — so the victim can dodge by stepping off the line before it arrives.
 * All references are weak (no GC keep needed).
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

	/** Everything about the damage that was true when the trigger was pulled and cannot be worked
	 *  out on arrival: heat, height advantage, target tags, the shooter's upgrades. Folded into one
	 *  number at registration because that is the moment those things applied. */
	float DamageMultiplier = 1.0f;

	/** What the shot was on course to hit, so a headshot stays a headshot when the bolt lands. The
	 *  bolt has no trace of its own on arrival, and without this every bolt is a body shot. */
	FName HitBoneName = NAME_None;

	/** What waits at the end of the line: the wall or the prop this shot lands on if nothing
	 *  intercepts it first. Its damage and its impact effect belong to the moment the bolt gets
	 *  there. Playing them when the trigger was pulled is what makes a dodged shot still spray
	 *  concrete off a wall the pellet had not reached yet. */
	FHitResult ImpactHit;
	bool bHasImpact = false;

	/** The streak drawn for this bolt on this machine, if there is one. The bolt puts it out when it
	 *  stops, so a pellet that buries itself in somebody does not go on flying towards the wall it
	 *  was aimed at. Weak: the tracer is free to finish and clean itself up on its own. */
	TWeakObjectPtr<UNiagaraComponent> Tracer;
};

/**
 * Ticks all active bolts. Centralised in a world subsystem so a bolt outlives the firing weapon's
 * frame and we avoid spawning a per-shot actor. Only ticks while bolts are in flight.
 */
UCLASS()
class POLARITY_API UEnemyBeamBoltSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:

	/** Register a new travelling bolt down an aim line. Damage is applied to Victim only if/when the
	 *  moving window reaches their current position within HitRadius. See FEnemyBeamBolt.
	 *
	 *  Victim may be null: a shot that is on course to hit nobody still travels, and still has to
	 *  arrive somewhere before its impact is allowed to happen. Pass ImpactHit for what is at the
	 *  end of the line so that arrival can be played there. */
	void RegisterBolt(AShooterWeapon* Weapon, AActor* Victim,
		const FVector& Start, const FVector& Dir, float MaxDist, float RandSpeed,
		float BeamLength, float HitRadius, float EnergyMultiplier,
		float DamageMultiplier = 1.0f, FName HitBoneName = NAME_None,
		const FHitResult& ImpactHit = FHitResult(), bool bHasImpact = false,
		UNiagaraComponent* Tracer = nullptr);

	// UTickableWorldSubsystem interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return ActiveBolts.Num() > 0; }

private:

	TArray<FEnemyBeamBolt> ActiveBolts;
};
