// AbilityHandler_HealGrenade.cpp

#include "AbilityHandler_HealGrenade.h"
#include "AbilityDefinition_HealGrenade.h"
#include "HealZone.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/HealGrenadeProjectile.h"
#include "Engine/World.h"

void UAbilityHandler_HealGrenade::SpawnThrown(const FVector& Location, const FRotator& Rotation)
{
	const UAbilityDefinition_HealGrenade* Def = Cast<UAbilityDefinition_HealGrenade>(GetDefinition());
	AShooterCharacter* Caster = GetOwningCharacter();
	if (!Caster || !Def)
	{
		return;
	}

	if (!Def->GrenadeClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] HealGrenade: no GrenadeClass set on %s"), *GetNameSafe(Def));
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Caster;
	SpawnParams.Instigator = Caster;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AHealGrenadeProjectile* Grenade = Caster->GetWorld()->SpawnActor<AHealGrenadeProjectile>(
		Def->GrenadeClass, Location, Rotation, SpawnParams);
	if (!Grenade)
	{
		return;
	}

	FHealGrenadePayload Payload;
	Payload.ZoneClass = Def->ZoneClass;
	Payload.MinHeal = Def->MinHeal;
	Payload.MaxHeal = Def->MaxHeal;
	Payload.MinDistance = Def->MinDistance;
	Payload.MaxDistance = Def->MaxDistance;
	Payload.ZoneRadius = Def->ZoneRadius;
	Payload.ZoneLifetime = Def->ZoneLifetime;

	Grenade->Launch(Def->ProjectileSpeed, Def->ProjectileGravityScale, Payload);
}
