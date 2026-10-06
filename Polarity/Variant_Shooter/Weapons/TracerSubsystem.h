// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TracerSubsystem.generated.h"

class AShooterProjectile;
class UNiagaraComponent;

/**
 * One drawn tracer: a world-space trail (the round's TrailFX) whose head is placed every frame on the
 * path the real round has flown, at a slightly earlier moment of its flight.
 *
 * Why not just attach the trail to the round: at 470-790 m/s a round crosses its first 25 m in one or
 * two frames, so an attached trail is on screen as something readable for a frame and then is a dot.
 * The head here runs behind the round near the gun and catches up by the round's CatchUpDistance
 * (time warp, see UTracerSubsystem::Warp), and after the round hits it keeps going until it reaches
 * the impact. Same idea as Valve's cosmetic tracer (source-sdk-2013 fx_tracer.cpp: it flies at its
 * own speed, slower than the bullet) but it lands on the real round instead of drifting from it.
 */
struct FActiveTracer
{
	TWeakObjectPtr<AShooterProjectile> Round;
	TWeakObjectPtr<UNiagaraComponent> Trail;

	/** Where the round was: xyz position, w seconds since it was fired. */
	TArray<FVector4> Path;
	int32 PathCursor = 0;

	double FireTime = 0.0;

	/** Flight time at which the round stopped (hit, timed out, went back to the pool). <0 while flying. */
	double EndTime = -1.0;

	/** Time warp: the head shows the round as it was at T * (t / T)^Exponent, so it starts slow and
	 *  meets the round at t = T (CatchUpDistance / speed). Exponent 1 = no lag. */
	float CatchUpTime = 0.0f;
	float WarpExponent = 1.0f;

	/** Own shot only: from the round's start to the muzzle the player sees. Shrinks to zero over
	 *  BarrelConvergeDistance of the head's travel or BarrelConvergeMaxTime, whichever comes first. */
	FVector BarrelOffset = FVector::ZeroVector;
	float BarrelConvergeDistance = 0.0f;
	float BarrelConvergeMaxTime = 0.0f;
};

/**
 * Draws the free tracers of rounds with AShooterProjectile::bFreeTracer. Purely cosmetic and local:
 * every machine draws the rounds it simulates, nothing is replicated. Only ticks while tracers fly.
 */
UCLASS()
class POLARITY_API UTracerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:

	/** A round was just fired. SeenMuzzleOffset is non-zero for the local player's own shot only
	 *  (AShooterWeapon::SpawnProjectileAtTransform). Does nothing for rounds without bFreeTracer. */
	void StartTracer(AShooterProjectile* Round, const FVector& SeenMuzzleOffset);

	/** The round stopped at FinalLocation. Its tracer carries on until it gets there. Safe to call for
	 *  a round that has no tracer, and more than once. */
	void EndTracer(AShooterProjectile* Round, const FVector& FinalLocation);

	// UTickableWorldSubsystem interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return Tracers.Num() > 0; }

	/** The exponent n of tau = T*(t/T)^n whose largest lag (t - tau) is LagOverT * T, limited by the
	 *  head's speed when it meets the round: n times the round's speed, at most MaxSpeedRatio. */
	static float SolveWarpExponent(float LagOverT, float MaxSpeedRatio);

private:

	/** Flight time the head shows at real flight time T. */
	static double Warp(const FActiveTracer& Tracer, double FlightTime);

	/** The round's position at flight time Time, from the recorded path. */
	static FVector SamplePath(FActiveTracer& Tracer, double Time);

	TArray<FActiveTracer> Tracers;

	/** Round classes whose effective lag has been logged once already ([TRACER_DEBUG]). */
	TSet<FName> LoggedRoundClasses;
};
