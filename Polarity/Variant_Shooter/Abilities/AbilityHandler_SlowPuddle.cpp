// AbilityHandler_SlowPuddle.cpp

#include "AbilityHandler_SlowPuddle.h"
#include "AbilityDefinition_SlowPuddle.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/SlowPuddleProjectile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

void UAbilityHandler_SlowPuddle::OnPerShotEffect_Implementation()
{
	const UAbilityDefinition_SlowPuddle* Def = Cast<UAbilityDefinition_SlowPuddle>(GetDefinition());
	AShooterCharacter* Caster = GetOwningCharacter();
	if (!Caster || !Def)
	{
		return;
	}

	if (!Def->PuddleProjectileClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] SlowPuddle: no PuddleProjectileClass set on %s"),
			*GetNameSafe(Def));
		return;
	}

	// Same muzzle the burst uses, so the bolt leaves the hand the animation is throwing with rather
	// than from the camera.
	FVector SpawnLoc = Caster->GetPawnViewLocation();
	if (const USkeletalMeshComponent* FPMesh = Caster->GetFirstPersonMesh())
	{
		if (!Def->ProjectileSpawnSocket.IsNone())
		{
			SpawnLoc = FPMesh->GetSocketLocation(Def->ProjectileSpawnSocket);
		}
	}

	// Straight down the player's aim. No enemy is chosen and none is needed: the throw is at a place.
	const FRotator Facing = Caster->GetBaseAimRotation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Caster;
	SpawnParams.Instigator = Caster;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ASlowPuddleProjectile* Bolt = Caster->GetWorld()->SpawnActor<ASlowPuddleProjectile>(
		Def->PuddleProjectileClass, SpawnLoc, Facing, SpawnParams);
	if (!Bolt)
	{
		return;
	}

	Bolt->Launch(Def->ProjectileSpeed, Def->PuddleClass, Def->PuddleRadius, Def->PuddleDuration,
		Def->MoveSpeedMultiplier);

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] SlowPuddle: bolt away from socket %s"),
		*Def->ProjectileSpawnSocket.ToString());
}
