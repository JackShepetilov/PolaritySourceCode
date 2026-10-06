// AbilityHandler_SmokeScreen.cpp

#include "AbilityHandler_SmokeScreen.h"
#include "AbilityDefinition_SmokeScreen.h"
#include "SmokeCloud.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/SmokeCanisterProjectile.h"
#include "Engine/World.h"

void UAbilityHandler_SmokeScreen::SpawnThrown(const FVector& Location, const FRotator& Rotation)
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

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Caster;
	SpawnParams.Instigator = Caster;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ASmokeCanisterProjectile* Canister = Caster->GetWorld()->SpawnActor<ASmokeCanisterProjectile>(
		Def->CanisterProjectileClass, Location, Rotation, SpawnParams);
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

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] SmokeScreen: canister away from %s"), *Location.ToCompactString());
}
