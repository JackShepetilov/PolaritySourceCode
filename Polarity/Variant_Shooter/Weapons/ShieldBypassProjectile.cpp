// ShieldBypassProjectile.cpp

#include "ShieldBypassProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Variant_Shooter/AI/ShooterNPC.h"

AShieldBypassProjectile::AShieldBypassProjectile()
{
	// The bolt deals NOTHING. AShooterProjectile ships with HitDamage = 25 and applies it in
	// ProcessHit, so a projectile that is supposed to be a delivery mechanism silently became a
	// weapon. This ability's whole point is that it changes what OTHER fire is worth.
	HitDamage = 0.0f;

	if (ProjectileMovement)
	{
		// No arc and no drift: this is a guided bolt, not a thrown object. Gravity off so the flight
		// reads as deliberate, and homing on so it cannot be dodged by strafing -- the ability is
		// paid for by its cooldown, not by aim.
		ProjectileMovement->ProjectileGravityScale = 0.0f;
		ProjectileMovement->bIsHomingProjectile = true;
		ProjectileMovement->bRotationFollowsVelocity = true;
	}
}

void AShieldBypassProjectile::LaunchAt(AActor* Target, float Speed, float DamageMultiplier,
	float SlowDuration, float SlowMultiplier)
{
	LockedTarget = Target;
	ConversionMultiplier = DamageMultiplier;
	ArrivalSlowDuration = SlowDuration;
	ArrivalSlowMultiplier = SlowMultiplier;

	if (!ProjectileMovement)
	{
		return;
	}

	if (Target && Target->GetRootComponent())
	{
		ProjectileMovement->HomingTargetComponent = Target->GetRootComponent();

		// Steering strong enough that the bolt actually arrives. A weak acceleration here reads as
		// the projectile "trying" to home and failing, which is worse than no homing at all.
		ProjectileMovement->HomingAccelerationMagnitude = Speed * 8.0f;
	}

	ProjectileMovement->InitialSpeed = Speed;
	ProjectileMovement->MaxSpeed = Speed;
	ProjectileMovement->Velocity = GetActorForwardVector() * Speed;
}

void AShieldBypassProjectile::ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp,
	const FVector& HitLocation, const FVector& HitDirection)
{
	AShooterNPC* Enemy = Cast<AShooterNPC>(HitActor);

	// Anything that is not the enemy it was aimed at just stops it. Converting on a wall would spend
	// the cooldown for nothing, and converting on a DIFFERENT enemy would quietly retarget an
	// ability whose whole cost is choosing who to open.
	if (!Enemy || (LockedTarget.IsValid() && Enemy != LockedTarget.Get()))
	{
		Super::ProcessHit(HitActor, HitComp, HitLocation, HitDirection);
		return;
	}

	if (HasAuthority() && !Enemy->IsDead())
	{
		// Opens a window; deals nothing itself. For the length of it, the ionization that would have
		// gone into this enemy's shield goes into its health instead, multiplied. The ability does
		// not kill anything on its own -- it changes what the team's existing fire is worth.
		Enemy->ApplyShieldBypass(ArrivalSlowDuration, ArrivalSlowMultiplier, ConversionMultiplier);

		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldBypass bolt hit %s: open %.1fs, redirect x%.1f, slow x%.2f"),
			*Enemy->GetName(), ArrivalSlowDuration, ConversionMultiplier, ArrivalSlowMultiplier);
	}

	Super::ProcessHit(HitActor, HitComp, HitLocation, HitDirection);
}
