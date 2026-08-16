// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "EnemyBeamBoltSubsystem.h"
#include "ShooterWeapon.h"
#include "NiagaraComponent.h"

void UEnemyBeamBoltSubsystem::RegisterBolt(AShooterWeapon* Weapon, AActor* Victim,
	const FVector& Start, const FVector& Dir, float MaxDist, float RandSpeed,
	float BeamLength, float HitRadius, float EnergyMultiplier,
	float DamageMultiplier, FName HitBoneName,
	const FHitResult& ImpactHit, bool bHasImpact,
	UNiagaraComponent* Tracer)
{
	// A victimless bolt is legitimate: it carries an impact that has to wait until it arrives.
	if (!Weapon || (!Victim && !bHasImpact))
	{
		return;
	}

	FEnemyBeamBolt Bolt;
	Bolt.Weapon = Weapon;
	Bolt.Victim = Victim;
	Bolt.Start = Start;
	Bolt.Dir = Dir.GetSafeNormal();
	Bolt.MaxDist = MaxDist;
	Bolt.RandSpeed = FMath::Max(RandSpeed, 1.0f);
	Bolt.BeamLength = FMath::Max(BeamLength, 1.0f);
	Bolt.HitRadius = HitRadius;
	Bolt.EnergyMultiplier = EnergyMultiplier;
	Bolt.DamageMultiplier = DamageMultiplier;
	Bolt.HitBoneName = HitBoneName;
	Bolt.ImpactHit = ImpactHit;
	Bolt.bHasImpact = bHasImpact;
	Bolt.Tracer = Tracer;
	Bolt.Age = 0.0f;

	ActiveBolts.Add(Bolt);
}

void UEnemyBeamBoltSubsystem::Tick(float DeltaTime)
{
	for (int32 i = ActiveBolts.Num() - 1; i >= 0; --i)
	{
		FEnemyBeamBolt& Bolt = ActiveBolts[i];
		Bolt.Age += DeltaTime;

		AActor* Victim = Bolt.Victim.Get();
		AShooterWeapon* Weapon = Bolt.Weapon.Get();
		if (!Weapon || (!Victim && !Bolt.bHasImpact))
		{
			ActiveBolts.RemoveAtSwap(i);
			continue;
		}

		const float Front = Bolt.RandSpeed * Bolt.Age;

		// Arrived at the end of its line without anybody intercepting it. THIS is when a shot that
		// hit nothing but scenery is allowed to mark the wall, and when a prop takes the damage:
		// the pellet is only here now. Something that stepped out of the way is not hurt by it.
		if (Front >= Bolt.MaxDist)
		{
			if (Bolt.bHasImpact)
			{
				AActor* ImpactActor = Bolt.ImpactHit.GetActor();
				// No DamageMultiplier here on purpose: that stack was measured against the pawn this
				// bolt was aimed at, and an instant shot never applied it to scenery either.
				if (ImpactActor && !Cast<APawn>(ImpactActor) && ImpactActor->CanBeDamaged())
				{
					Weapon->ApplyHitscanDamage(Bolt.ImpactHit, Bolt.EnergyMultiplier,
						Bolt.ImpactHit.Distance, 0.0f);
				}
				Weapon->SpawnImpactEffect(Bolt.ImpactHit);
			}

			ActiveBolts.RemoveAtSwap(i);
			continue;
		}

		if (!Victim)
		{
			continue;
		}

		// Project the victim's CURRENT position onto the frozen beam line.
		const FVector Rel = Victim->GetActorLocation() - Bolt.Start;
		const float Dp = FVector::DotProduct(Rel, Bolt.Dir);
		const float Perp = (Rel - Bolt.Dir * Dp).Size();

		const bool bInWindow = (Dp >= 0.0f) && (Dp <= Bolt.MaxDist)
			&& (Dp <= Front) && (Dp >= Front - Bolt.BeamLength);

		if (bInWindow && Perp <= Bolt.HitRadius)
		{
			// Synthesize a hit on the victim's CURRENT position and route through the normal
			// hitscan damage path (friend access to the protected method). The bone is the one the
			// shot was on course for: there is no trace to ask on arrival.
			FHitResult Hit(Victim, nullptr, Victim->GetActorLocation(), -Bolt.Dir);
			Hit.Distance = Dp;
			Hit.BoneName = Bolt.HitBoneName;
			Weapon->ApplyHitscanDamage(Hit, Bolt.EnergyMultiplier, Dp, 0.0f, Bolt.DamageMultiplier);

			// The pellet stopped in the body: the impact belongs here and now, whatever was behind
			// never gets hit, and the streak stops here rather than carrying on to the wall it was
			// pointed at. Deactivate, not destroy, so the part already drawn fades out instead of
			// blinking away.
			Weapon->SpawnImpactEffect(Hit);
			if (UNiagaraComponent* BoltTracer = Bolt.Tracer.Get())
			{
				BoltTracer->Deactivate();
			}

			ActiveBolts.RemoveAtSwap(i);
		}
	}
}

TStatId UEnemyBeamBoltSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEnemyBeamBoltSubsystem, STATGROUP_Tickables);
}
