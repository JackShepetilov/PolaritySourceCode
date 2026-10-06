// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "TracerSubsystem.h"

#include "ShooterProjectile.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

namespace TracerSubsystemPrivate
{
	/** A tracer that has not finished by now is dropped whatever it is doing (rounds time out at 10 s). */
	constexpr double MaxTracerSeconds = 15.0;

	/** One step of simulation right after the trail is made, so its first point lies at the barrel. */
	constexpr float FirstPointStep = 1.0f / 240.0f;

	/** Largest lag of tau = T*(t/T)^N, as a fraction of T: at x = N^(-1/(N-1)), x - x^N = x * (1 - 1/N). */
	double MaxLagFraction(double N)
	{
		if (N <= 1.0)
		{
			return 0.0;
		}
		const double X = FMath::Pow(N, -1.0 / (N - 1.0));
		return X * (1.0 - 1.0 / N);
	}
}

float UTracerSubsystem::SolveWarpExponent(float LagOverT, float MaxSpeedRatio)
{
	using namespace TracerSubsystemPrivate;

	const double MaxN = FMath::Max(1.0, static_cast<double>(MaxSpeedRatio));
	if (LagOverT <= 0.0f || MaxN <= 1.0)
	{
		return 1.0f;
	}

	// Asked for more lag than the speed cap allows: take what the cap gives.
	if (MaxLagFraction(MaxN) <= LagOverT)
	{
		return static_cast<float>(MaxN);
	}

	// MaxLagFraction grows with N, so bisect.
	double Lo = 1.0;
	double Hi = MaxN;
	for (int32 Step = 0; Step < 40; ++Step)
	{
		const double Mid = 0.5 * (Lo + Hi);
		if (MaxLagFraction(Mid) < LagOverT)
		{
			Lo = Mid;
		}
		else
		{
			Hi = Mid;
		}
	}
	return static_cast<float>(Hi);
}

double UTracerSubsystem::Warp(const FActiveTracer& Tracer, double FlightTime)
{
	if (Tracer.WarpExponent <= 1.0f || Tracer.CatchUpTime <= 0.0f || FlightTime >= Tracer.CatchUpTime)
	{
		return FlightTime;
	}
	const double X = FMath::Max(FlightTime, 0.0) / Tracer.CatchUpTime;
	return Tracer.CatchUpTime * FMath::Pow(X, static_cast<double>(Tracer.WarpExponent));
}

FVector UTracerSubsystem::SamplePath(FActiveTracer& Tracer, double Time)
{
	const int32 Num = Tracer.Path.Num();
	if (Num == 0)
	{
		return FVector::ZeroVector;
	}

	// The time asked for only ever grows, so the cursor only walks forward.
	while (Tracer.PathCursor + 1 < Num && Tracer.Path[Tracer.PathCursor + 1].W <= Time)
	{
		++Tracer.PathCursor;
	}

	const FVector4& A = Tracer.Path[Tracer.PathCursor];
	if (Tracer.PathCursor + 1 >= Num)
	{
		return FVector(A);
	}

	const FVector4& B = Tracer.Path[Tracer.PathCursor + 1];
	const double Span = B.W - A.W;
	const double Alpha = (Span > UE_SMALL_NUMBER) ? FMath::Clamp((Time - A.W) / Span, 0.0, 1.0) : 1.0;
	return FMath::Lerp(FVector(A), FVector(B), Alpha);
}

void UTracerSubsystem::StartTracer(AShooterProjectile* Round, const FVector& SeenMuzzleOffset)
{
	using namespace TracerSubsystemPrivate;

	UWorld* World = GetWorld();
	if (!IsValid(Round) || !World || !Round->bFreeTracer || !Round->TrailFX
		|| World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// A round fired again before its last tracer heard it stop: close that one where it was last seen.
	for (FActiveTracer& Old : Tracers)
	{
		if (Old.EndTime < 0.0 && Old.Round.Get() == Round)
		{
			Old.EndTime = Old.Path.Num() > 0 ? Old.Path.Last().W : 0.0;
			Old.Round = nullptr;
		}
	}

	const UProjectileMovementComponent* const Movement = Round->FindComponentByClass<UProjectileMovementComponent>();
	const float Speed = Movement ? Movement->InitialSpeed : 0.0f;
	const FVector Start = Round->GetActorLocation();

	FActiveTracer& Tracer = Tracers.AddDefaulted_GetRef();
	Tracer.Round = Round;
	Tracer.FireTime = World->GetTimeSeconds();
	Tracer.Path.Add(FVector4(Start, 0.0));
	Tracer.CatchUpTime = (Speed > KINDA_SMALL_NUMBER) ? Round->TracerCatchUpDistance / Speed : 0.0f;
	const float LagOverT = (Tracer.CatchUpTime > KINDA_SMALL_NUMBER) ? Round->TracerMaxLag / Tracer.CatchUpTime : 0.0f;
	Tracer.WarpExponent = SolveWarpExponent(LagOverT, Round->TracerMaxCatchUpSpeedRatio);
	Tracer.BarrelOffset = SeenMuzzleOffset;
	Tracer.BarrelConvergeDistance = Round->TracerBarrelConvergeDistance;
	Tracer.BarrelConvergeMaxTime = Round->TracerBarrelConvergeMaxTime;

	// What the settings actually give for this class, once: a lag the speed cap cannot reach is cut.
	const FName ClassName = Round->GetClass()->GetFName();
	if (!LoggedRoundClasses.Contains(ClassName))
	{
		LoggedRoundClasses.Add(ClassName);
		UE_LOG(LogTemp, Log,
			TEXT("[TRACER_DEBUG] %s: speed %.0f cm/s, catch-up %.0f cm in %.0f ms, asked lag %.0f ms, ")
			TEXT("effective lag %.0f ms, head speed at catch-up %.1fx"),
			*ClassName.ToString(), Speed, Round->TracerCatchUpDistance, Tracer.CatchUpTime * 1000.0f,
			Round->TracerMaxLag * 1000.0f,
			TracerSubsystemPrivate::MaxLagFraction(Tracer.WarpExponent) * Tracer.CatchUpTime * 1000.0,
			Tracer.WarpExponent);
	}

	UNiagaraComponent* const Trail = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World, Round->TrailFX, Start + SeenMuzzleOffset, FRotator::ZeroRotator, FVector::OneVector,
		/*bAutoDestroy*/ true, /*bAutoActivate*/ true, ENCPoolMethod::None, /*bPreCullCheck*/ false);
	if (Trail)
	{
		// The system turns its User.TrailLength into a point lifetime with this (cleanup only).
		Trail->SetVariableFloat(TEXT("TrailSpeed"), Speed);
		// Where the trail starts, so M_TracerRibbon can fade the end near the shooter (TailFadeLength).
		Trail->SetVariablePosition(TEXT("TrailStart"), Start + SeenMuzzleOffset);
		Trail->AdvanceSimulation(1, FirstPointStep);
	}
	Tracer.Trail = Trail;
}

void UTracerSubsystem::EndTracer(AShooterProjectile* Round, const FVector& FinalLocation)
{
	UWorld* World = GetWorld();
	if (!Round || !World)
	{
		return;
	}

	for (FActiveTracer& Tracer : Tracers)
	{
		if (Tracer.EndTime < 0.0 && Tracer.Round.Get() == Round)
		{
			const double Flight = FMath::Max(World->GetTimeSeconds() - Tracer.FireTime, Tracer.Path.Last().W);
			Tracer.Path.Add(FVector4(FinalLocation, Flight));
			Tracer.EndTime = Flight;
			Tracer.Round = nullptr;
		}
	}
}

void UTracerSubsystem::Tick(float DeltaTime)
{
	using namespace TracerSubsystemPrivate;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	for (int32 i = Tracers.Num() - 1; i >= 0; --i)
	{
		FActiveTracer& Tracer = Tracers[i];
		const double Flight = Now - Tracer.FireTime;

		// Record where the round is while it still flies.
		if (Tracer.EndTime < 0.0)
		{
			if (const AShooterProjectile* Round = Tracer.Round.Get())
			{
				if (Flight > Tracer.Path.Last().W)
				{
					Tracer.Path.Add(FVector4(Round->GetActorLocation(), Flight));
				}
			}
			else
			{
				// Gone without telling (destroyed): it ended where it was last seen.
				Tracer.EndTime = Tracer.Path.Last().W;
			}
		}

		UNiagaraComponent* const Trail = Tracer.Trail.Get();
		if (!Trail || Flight > MaxTracerSeconds)
		{
			if (Trail)
			{
				Trail->Deactivate();
			}
			Tracers.RemoveAtSwap(i);
			continue;
		}

		double Shown = Warp(Tracer, Flight);
		const bool bArrived = Tracer.EndTime >= 0.0 && Shown >= Tracer.EndTime;
		if (Tracer.EndTime >= 0.0)
		{
			Shown = FMath::Min(Shown, Tracer.EndTime);
		}

		FVector Head = SamplePath(Tracer, Shown);
		if (!Tracer.BarrelOffset.IsNearlyZero())
		{
			const double Travelled = FVector::Dist(Head, FVector(Tracer.Path[0]));
			const double ByDistance = (Tracer.BarrelConvergeDistance > 0.0f) ? Travelled / Tracer.BarrelConvergeDistance : 1.0;
			const double ByTime = (Tracer.BarrelConvergeMaxTime > 0.0f) ? Flight / Tracer.BarrelConvergeMaxTime : 1.0;
			Head += Tracer.BarrelOffset * FMath::Clamp(1.0 - FMath::Max(ByDistance, ByTime), 0.0, 1.0);
		}
		Trail->SetWorldLocation(Head);

		// Reached the impact: stop laying points and let the trail fade out (the system destroys itself).
		if (bArrived)
		{
			Trail->Deactivate();
			Tracers.RemoveAtSwap(i);
		}
	}
}

TStatId UTracerSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTracerSubsystem, STATGROUP_Tickables);
}
