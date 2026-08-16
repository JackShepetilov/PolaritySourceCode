// ShieldBypassProjectile.cpp

#include "ShieldBypassProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "EMFVelocityModifier.h"
#include "Engine/DamageEvents.h"

AShieldBypassProjectile::AShieldBypassProjectile()
{
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
		// The conversion. Shield IS the ionization sitting on the enemy, so the bolt takes all of it
		// and pays it out as health damage -- that is what "skip the opening phase" means: the work
		// the team already did on the shield is cashed in rather than continued.
		float Converted = 0.0f;
		if (UEMFVelocityModifier* Modifier = Enemy->FindComponentByClass<UEMFVelocityModifier>())
		{
			Converted = FMath::Abs(Modifier->GetCharge());
			Modifier->SetCharge(0.0f);
		}

		const float Damage = Converted * ConversionMultiplier;
		if (Damage > 0.0f)
		{
			FPointDamageEvent DamageEvent;
			DamageEvent.DamageTypeClass = UDamageType::StaticClass();
			Enemy->TakeDamage(Damage, DamageEvent, GetInstigatorController(), GetOwner());
		}

		// And the slow, so an enemy stripped of its shield stays where the rest of the team can act
		// on it rather than walking away from everyone who is not the caster.
		if (ArrivalSlowDuration > 0.0f)
		{
			Enemy->ApplyShieldBypass(ArrivalSlowDuration, ArrivalSlowMultiplier, 1.0f);
		}

		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldBypass bolt hit %s: converted %.1f charge into %.1f damage"),
			*Enemy->GetName(), Converted, Damage);
	}

	Super::ProcessHit(HitActor, HitComp, HitLocation, HitDirection);
}
