// AbilityHandler_SmokeScreen.cpp

#include "AbilityHandler_SmokeScreen.h"
#include "AbilityDefinition_SmokeScreen.h"
#include "SmokeCloud.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/SmokeCanisterProjectile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

void UAbilityHandler_SmokeScreen::OnPerShotEffect_Implementation()
{
	const UAbilityDefinition_SmokeScreen* Def = Cast<UAbilityDefinition_SmokeScreen>(GetDefinition());
	AShooterCharacter* Caster = GetOwningCharacter();
	if (!Caster || !Def)
	{
		return;
	}

	if (!Def->CanisterProjectileClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] SmokeScreen: no CanisterProjectileClass set on %s"),
			*GetNameSafe(Def));
		return;
	}

	// Same muzzle the burst uses, so the canister leaves the hand the animation is throwing with
	// rather than from the camera.
	FVector SpawnLoc = Caster->GetPawnViewLocation();
	if (const USkeletalMeshComponent* FPMesh = Caster->GetFirstPersonMesh())
	{
		if (!Def->ProjectileSpawnSocket.IsNone())
		{
			SpawnLoc = FPMesh->GetSocketLocation(Def->ProjectileSpawnSocket);
		}
	}

	const FRotator Facing = Caster->GetBaseAimRotation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Caster;
	SpawnParams.Instigator = Caster;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ASmokeCanisterProjectile* Canister = Caster->GetWorld()->SpawnActor<ASmokeCanisterProjectile>(
		Def->CanisterProjectileClass, SpawnLoc, Facing, SpawnParams);
	if (!Canister)
	{
		return;
	}

	// One cloud's worth of numbers. Where the three of them end up is worked out by the canister on
	// landing, from the direction it was actually travelling.
	FSmokeCloudShape Shape;
	Shape.Radius = Def->CloudRadius;
	Shape.GrowTime = Def->GrowTime;
	Shape.Duration = Def->Duration;
	Shape.FadeTime = Def->FadeTime;

	Canister->Launch(Def->ProjectileSpeed, Def->ProjectileGravityScale, Def->CloudClass, Shape,
		Def->SightPenetration, Def->TurnRateMultiplier, Def->SplitCount, Def->SplitSpacing);

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] SmokeScreen: canister away from socket %s"),
		*Def->ProjectileSpawnSocket.ToString());
}
