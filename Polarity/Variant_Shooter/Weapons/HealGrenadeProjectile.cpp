// HealGrenadeProjectile.cpp

#include "HealGrenadeProjectile.h"
#include "Variant_Shooter/Abilities/HealZone.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/World.h"

AHealGrenadeProjectile::AHealGrenadeProjectile()
{
	// An EXPLICIT zero, as on the smoke canister: the base ships HitDamage negative, meaning "take
	// the firing weapon's number", and a delivery mechanism must not pick up a gun's damage.
	HitDamage = 0.0f;
	CharacterKnockbackForce = 0.0f;
	KnockbackUpwardBias = 0.0f;

	PrimaryActorTick.bCanEverTick = false;

	// @see the class comment for why this one is replicated.
	bReplicates = true;
	SetReplicateMovement(true);

	if (ProjectileMovement)
	{
		ProjectileMovement->bIsHomingProjectile = false;
		ProjectileMovement->HomingTargetComponent = nullptr;
		ProjectileMovement->bRotationFollowsVelocity = true;
		ProjectileMovement->ProjectileGravityScale = 1.0f;
	}
}

void AHealGrenadeProjectile::Launch(float Speed, float GravityScale, const FHealGrenadePayload& InPayload)
{
	Payload = InPayload;
	LaunchLocation = GetActorLocation();

	if (ProjectileMovement)
	{
		ProjectileMovement->ProjectileGravityScale = GravityScale;
		ProjectileMovement->InitialSpeed = Speed;
		ProjectileMovement->MaxSpeed = Speed;
		ProjectileMovement->Velocity = GetActorForwardVector() * Speed;
	}
}

void AHealGrenadeProjectile::ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp,
	const FVector& HitLocation, const FVector& HitDirection)
{
	// Super is deliberately not called: all it does is damage and a shove, and this has neither.
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World)
	{
		return;
	}

	if (!Payload.ZoneClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] HealGrenade: burst with no ZoneClass set"));
		return;
	}

	const float Distance = FVector::Dist(LaunchLocation, HitLocation);
	const float Range = FMath::Max(1.0f, Payload.MaxDistance - Payload.MinDistance);
	const float Strength = FMath::Clamp((Distance - Payload.MinDistance) / Range, 0.0f, 1.0f);
	const float Heal = FMath::Lerp(Payload.MinHeal, Payload.MaxHeal, Strength);

	// Settle onto the floor when there is one close under the burst, otherwise stay where it burst.
	FVector ZoneLocation = HitLocation;
	if (FloorSearchDistance > 0.0f)
	{
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(this);

		FHitResult Floor;
		if (World->LineTraceSingleByChannel(Floor, HitLocation + FVector(0.0f, 0.0f, 20.0f),
			HitLocation - FVector(0.0f, 0.0f, FloorSearchDistance), ECC_WorldStatic, Params))
		{
			ZoneLocation = Floor.ImpactPoint + FVector(0.0f, 0.0f, ZoneHeightAboveFloor);
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = GetInstigator();

	if (AHealZone* Zone = World->SpawnActor<AHealZone>(Payload.ZoneClass, ZoneLocation, FRotator::ZeroRotator, SpawnParams))
	{
		Zone->Begin(Heal, Strength, Payload.ZoneRadius, Payload.ZoneLifetime);
	}

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] HealGrenade: burst on %s after %.0f cm, heal %.1f (strength %.2f)"),
		*GetNameSafe(HitActor), Distance, Heal, Strength);
}
