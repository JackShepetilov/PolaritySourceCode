// ShieldBypassProjectile.cpp

#include "ShieldBypassProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

AShieldBypassProjectile::AShieldBypassProjectile()
{
	// The bolt does NOTHING on contact. Not damage, not knockback.
	//
	// AShooterProjectile ships both switched on -- HitDamage 25 and CharacterKnockbackForce 1200 --
	// and applies them in ProcessHit, so a projectile meant purely as a delivery mechanism inherited
	// a weapon's behaviour twice over without anyone setting it. Every inherited default here is a
	// behaviour until it is explicitly turned off.
	//
	// This ability's whole point is that it changes what OTHER fire is worth. Anything the bolt does
	// on its own is wrong by definition.
	HitDamage = 0.0f;
	CharacterKnockbackForce = 0.0f;
	KnockbackUpwardBias = 0.0f;

	// Needed for the in-flight scan.
	PrimaryActorTick.bCanEverTick = true;

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

	// Set unconditionally, BEFORE any target exists: a bolt that latches on mid-flight would
	// otherwise steer with zero acceleration and fly straight past the enemy it just found.
	ProjectileMovement->HomingAccelerationMagnitude = Speed * 8.0f;

	if (Target && Target->GetRootComponent())
	{
		ProjectileMovement->HomingTargetComponent = Target->GetRootComponent();
	}

	ProjectileMovement->InitialSpeed = Speed;
	ProjectileMovement->MaxSpeed = Speed;
	ProjectileMovement->Velocity = GetActorForwardVector() * Speed;
}

void AShieldBypassProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Only the authority hunts. A client's copy follows the replicated flight and must not pick a
	// different victim than the machine that decides.
	if (!HasAuthority() || LockedTarget.IsValid())
	{
		return;
	}

	TimeSinceScan += DeltaSeconds;
	if (TimeSinceScan >= ScanInterval)
	{
		TimeSinceScan = 0.0f;
		ScanForTarget();
	}
}

void AShieldBypassProjectile::ScanForTarget()
{
	UWorld* World = GetWorld();
	if (!World || ScanRadius <= 0.0f)
	{
		return;
	}

	// Nearest live enemy, no cone and no line of sight test: the whole point of scanning in flight is
	// that the caster could NOT see the target when they fired -- round a corner, over a crate. A
	// visibility check here would rebuild exactly the restriction this removes.
	AShooterNPC* Best = nullptr;
	float BestDistSq = ScanRadius * ScanRadius;
	const FVector Here = GetActorLocation();

	for (TActorIterator<AShooterNPC> It(World); It; ++It)
	{
		AShooterNPC* Enemy = *It;
		if (!Enemy || Enemy->IsDead())
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(Here, Enemy->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Enemy;
		}
	}

	if (!Best)
	{
		return;
	}

	LockedTarget = Best;
	if (ProjectileMovement && Best->GetRootComponent())
	{
		ProjectileMovement->HomingTargetComponent = Best->GetRootComponent();
	}

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldBypass bolt latched onto %s in flight (%.0f away)"),
		*Best->GetName(), FMath::Sqrt(BestDistSq));
}

void AShieldBypassProjectile::Multicast_PlayImpactVFX_Implementation(FVector Location)
{
	if (!ImpactVFX)
	{
		return;
	}

	// Spawned at a location rather than attached: the bolt is about to be destroyed, and an attached
	// system would go with it mid-burst.
	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		ImpactVFX,
		Location,
		FRotator::ZeroRotator,
		FVector(1.0f),
		true,   // auto destroy
		true,   // auto activate
		ENCPoolMethod::None);
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

		// Fired from the same branch that opens the enemy, so the effect marks the ability landing and
		// not merely the bolt stopping. A wall hit stays silent: nothing happened there.
		Multicast_PlayImpactVFX(HitLocation);

		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldBypass bolt hit %s: open %.1fs, redirect x%.1f, slow x%.2f"),
			*Enemy->GetName(), ArrivalSlowDuration, ConversionMultiplier, ArrivalSlowMultiplier);
	}

	Super::ProcessHit(HitActor, HitComp, HitLocation, HitDirection);
}
