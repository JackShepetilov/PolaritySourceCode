// SlowPuddleProjectile.cpp

#include "SlowPuddleProjectile.h"
#include "Variant_Shooter/Abilities/SlowPuddle.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/World.h"

ASlowPuddleProjectile::ASlowPuddleProjectile()
{
	// The bolt does NOTHING on contact. Zero here is an EXPLICIT zero, not the inherit value: the
	// base ships HitDamage negative, meaning "take the firing weapon's number", and a projectile
	// meant purely as a delivery mechanism must not quietly pick up whatever gun launched it.
	HitDamage = 0.0f;
	CharacterKnockbackForce = 0.0f;
	KnockbackUpwardBias = 0.0f;

	PrimaryActorTick.bCanEverTick = false;

	if (ProjectileMovement)
	{
		// Straight, and no longer homing. The ability used to pick an enemy and steer at it; it now
		// picks a piece of floor, so the flight is the player's aim and nothing else. Gravity stays
		// off exactly as it was on the old bolt -- what changed is the steering, not the arc.
		ProjectileMovement->ProjectileGravityScale = 0.0f;
		ProjectileMovement->bIsHomingProjectile = false;
		ProjectileMovement->HomingTargetComponent = nullptr;
		ProjectileMovement->bRotationFollowsVelocity = true;
	}
}

void ASlowPuddleProjectile::Launch(float Speed, TSubclassOf<ASlowPuddle> InPuddleClass, float InRadius,
	float InDuration, float InSlowMultiplier)
{
	PuddleClass = InPuddleClass;
	PuddleRadius = InRadius;
	PuddleDuration = InDuration;
	PuddleSlowMultiplier = InSlowMultiplier;

	if (ProjectileMovement)
	{
		ProjectileMovement->InitialSpeed = Speed;
		ProjectileMovement->MaxSpeed = Speed;
		ProjectileMovement->Velocity = GetActorForwardVector() * Speed;
	}
}

FVector ASlowPuddleProjectile::FindFloorUnder(const FVector& ImpactPoint) const
{
	const UWorld* World = GetWorld();
	if (!World || FloorSearchDistance <= 0.0f)
	{
		return ImpactPoint;
	}

	// Started slightly above the impact so a hit that landed exactly on the floor does not begin the
	// trace inside it and miss.
	const FVector Start = ImpactPoint + FVector(0.0f, 0.0f, 20.0f);
	const FVector End = ImpactPoint - FVector(0.0f, 0.0f, FloorSearchDistance);

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	if (GetInstigator())
	{
		Params.AddIgnoredActor(GetInstigator());
	}

	FHitResult Floor;
	if (World->LineTraceSingleByChannel(Floor, Start, End, ECC_WorldStatic, Params))
	{
		return Floor.ImpactPoint;
	}

	// Nothing under it -- shot out over a gap. Leave the puddle where the bolt stopped rather than
	// swallowing the cast: a puddle in a strange place is a bad throw, no puddle at all is a bug.
	return ImpactPoint;
}

void ASlowPuddleProjectile::ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp,
	const FVector& HitLocation, const FVector& HitDirection)
{
	if (HasAuthority() && PuddleClass)
	{
		const FVector PuddleLocation = FindFloorUnder(HitLocation);

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.Owner = GetOwner();
		SpawnParams.Instigator = GetInstigator();

		if (ASlowPuddle* Puddle = GetWorld()->SpawnActor<ASlowPuddle>(
			PuddleClass, PuddleLocation, FRotator::ZeroRotator, SpawnParams))
		{
			Puddle->Begin(PuddleRadius, PuddleDuration, PuddleSlowMultiplier);

			UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] SlowPuddle: dropped at %s (r=%.0f, %.1fs, x%.2f)"),
				*PuddleLocation.ToCompactString(), PuddleRadius, PuddleDuration, PuddleSlowMultiplier);
		}
	}
	else if (HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] SlowPuddle: bolt landed with no PuddleClass set"));
	}

	Super::ProcessHit(HitActor, HitComp, HitLocation, HitDirection);
}
