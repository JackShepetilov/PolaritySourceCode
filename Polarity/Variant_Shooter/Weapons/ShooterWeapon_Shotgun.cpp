// ShooterWeapon_Shotgun.cpp

#include "ShooterWeapon_Shotgun.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "GameFramework/Pawn.h"

AShooterWeapon_Shotgun::AShooterWeapon_Shotgun()
{
	// Three pellets in a triangle, point up: the Mozambique's pattern. Unit-circle offsets, so
	// PelletSpreadAngle alone decides how big the triangle is.
	PelletPattern = {
		FVector2D(0.0f, 1.0f),
		FVector2D(-0.866f, -0.5f),
		FVector2D(0.866f, -0.5f)
	};

	// Hitscan, and specifically the thin-ray path. WaveDivergence models a shot as ONE widening
	// cone, which is another way to build a shotgun and would fight this one: the pellets would
	// each grow a cone of their own and the pattern would stop meaning anything.
	bUseHitscan = true;
	WaveDivergence = 0.0f;

	// The pattern is the spread. AimVariance stays available for a weapon that should wander as a
	// whole, but a Mozambique does not: it puts the same triangle in the same place every time.
	AimVariance = 0.0f;

	// 17 a pellet, so 51 for a clean hit and 63 with the head multiplier on all three; five in the
	// magazine; ~160 rounds a minute, held down rather than clicked.
	HitscanDamage = 17.0f;
	HeadshotMultiplier = 1.25f;
	MagazineSize = 5;
	RefireRate = 0.375f;
	bFullAuto = true;
	FiringRecoil = 2.5f;

	// Nothing on the trace falls off with distance, so range is what keeps this a close-quarters
	// weapon: past here the pellets simply stop. The spread does the rest of the work before that.
	MaxHitscanRange = 5000.0f;

	// Pellets are not instant. In Apex a shotgun pellet is a projectile you can see travel, and this
	// project already has that shape of thing: the bolt, a hit decided at the trigger that lands
	// when it arrives and can be stepped out of on the way. Each pellet registers one of its own,
	// and the tracer is timed off the same numbers, so the streak IS the pellet.
	//
	// 25400 cm/s is the Apex pellet speed (10000 hammer units a second). No variance: three pellets
	// of one shot arriving at three different times reads as a bug, not as buckshot. 900 cm of
	// streak keeps the tail at the barrel for the first frames, so the shot leaves the gun instead
	// of appearing three metres in front of it.
	bHitscanTravelsAsBolt = true;
	HitscanBoltSpeed = 25400.0f;
	HitscanBoltSpeedVariance = 0.0f;
	HitscanBoltLength = 900.0f;

	// Five rounds are only a weakness if running out costs something, so this is the first weapon
	// in the project with a magazine that has to be filled. 2.6 s is the Mozambique's full reload.
	bUseReload = true;
	ReloadTime = 2.6f;
}

FVector AShooterWeapon_Shotgun::GetPelletDirection(const FVector& AimDirection, const FVector2D& PatternOffset) const
{
	if (PelletSpreadAngle <= 0.0f || PatternOffset.IsNearlyZero())
	{
		return AimDirection;
	}

	// The offset is applied across a unit-length direction, so the angle enters as its tangent --
	// the same construction AimVariance uses in the base class.
	const FRotationMatrix AimBasis(AimDirection.Rotation());
	const FVector Right = AimBasis.GetUnitAxis(EAxis::Y);
	const FVector Up = AimBasis.GetUnitAxis(EAxis::Z);

	const float SpreadTangent = FMath::Tan(FMath::DegreesToRadians(PelletSpreadAngle));

	return (AimDirection
		+ Right * (PatternOffset.X * SpreadTangent)
		+ Up * (PatternOffset.Y * SpreadTangent)).GetSafeNormal();
}

void AShooterWeapon_Shotgun::FireHitscan(const FVector& TargetLocation)
{
	// No pattern authored: nothing to spread, and one pellet down the aim line is exactly what the
	// base class already does.
	if (PelletPattern.Num() == 0)
	{
		Super::FireHitscan(TargetLocation);
		return;
	}

	// The aim line is resolved ONCE and every pellet is turned off it. Resolving it per pellet
	// would re-read the camera and the muzzle between traces and let the pattern drift while the
	// player turns.
	FVector Start;
	FVector AimDirection;
	ResolveHitscanRay(TargetLocation, Start, AimDirection);

	// NPCs trace from the muzzle without the cone filter, exactly as the base class routes them.
	const bool bNPCShooter = PawnOwner && !PawnOwner->IsPlayerControlled();

	for (const FVector2D& PatternOffset : PelletPattern)
	{
		const FVector PelletDirection = GetPelletDirection(AimDirection, PatternOffset);

		if (bNPCShooter)
		{
			PerformSimpleHitscan(Start, PelletDirection, 1.0f);
		}
		else
		{
			PerformHitscan(Start, PelletDirection, 1.0f, 0);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[SHOTGUN_DEBUG] %s: %d pellets, spread %.2f deg, %.0f damage each"),
		*GetName(), PelletPattern.Num(), PelletSpreadAngle, HitscanDamage);

	// Once per trigger pull, not once per pellet.
	ConsumeRoundAfterShot();
}

void AShooterWeapon_Shotgun::FireProjectile(const FVector& TargetLocation, float ChargeMultiplier)
{
	if (PelletPattern.Num() == 0)
	{
		Super::FireProjectile(TargetLocation, ChargeMultiplier);
		return;
	}

	// Same muzzle for every pellet, different direction: they leave one barrel.
	const FTransform AimTransform = CalculateProjectileSpawnTransform(TargetLocation);
	const FVector AimDirection = AimTransform.GetRotation().GetForwardVector();

	AShooterCharacter* OwnerCharacter = Cast<AShooterCharacter>(PawnOwner);

	for (const FVector2D& PatternOffset : PelletPattern)
	{
		const FVector PelletDirection = GetPelletDirection(AimDirection, PatternOffset);
		const FTransform PelletTransform(PelletDirection.Rotation(), AimTransform.GetLocation(), FVector::OneVector);

		if (HasAuthority())
		{
			SpawnProjectileAtTransform(PelletTransform, ChargeMultiplier, /*bCosmeticOnly*/ false);
		}
		else
		{
			// The same split the base class makes, once per pellet: the shooter sees its own pellet
			// leave the barrel immediately and asks the server for the real one in the same breath.
			SpawnProjectileAtTransform(PelletTransform, ChargeMultiplier, /*bCosmeticOnly*/ true);

			if (OwnerCharacter)
			{
				OwnerCharacter->Server_FireProjectile(this, PelletTransform, ChargeMultiplier);
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[SHOTGUN_DEBUG] %s: %d projectile pellets, spread %.2f deg"),
		*GetName(), PelletPattern.Num(), PelletSpreadAngle);

	ConsumeRoundAfterShot();
}
