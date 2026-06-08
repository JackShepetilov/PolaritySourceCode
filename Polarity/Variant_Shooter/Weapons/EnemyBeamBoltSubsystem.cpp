// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "EnemyBeamBoltSubsystem.h"
#include "ShooterWeapon.h"

void UEnemyBeamBoltSubsystem::RegisterBolt(AShooterWeapon* Weapon, AActor* Victim,
	const FVector& Start, const FVector& Dir, float MaxDist, float RandSpeed,
	float BeamLength, float HitRadius, float EnergyMultiplier)
{
	if (!Weapon || !Victim)
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
		if (!Victim || !Weapon)
		{
			ActiveBolts.RemoveAtSwap(i);
			continue;
		}

		const float Front = Bolt.RandSpeed * Bolt.Age;

		// Expired: the trailing edge has passed the end of the beam without connecting (= dodged).
		if (Front - Bolt.BeamLength > Bolt.MaxDist)
		{
			ActiveBolts.RemoveAtSwap(i);
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
			// hitscan damage path (friend access to the protected method).
			FHitResult Hit(Victim, nullptr, Victim->GetActorLocation(), -Bolt.Dir);
			Hit.Distance = Dp;
			Weapon->ApplyHitscanDamage(Hit, Bolt.EnergyMultiplier, Dp, 0.0f);
			ActiveBolts.RemoveAtSwap(i);
		}
	}
}

TStatId UEnemyBeamBoltSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEnemyBeamBoltSubsystem, STATGROUP_Tickables);
}
