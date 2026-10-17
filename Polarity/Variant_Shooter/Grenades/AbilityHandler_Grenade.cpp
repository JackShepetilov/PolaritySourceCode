// AbilityHandler_Grenade.cpp

#include "Variant_Shooter/Grenades/AbilityHandler_Grenade.h"
#include "Variant_Shooter/Grenades/GrenadeDefinition.h"
#include "Variant_Shooter/Grenades/GrenadeProjectile.h"
#include "Variant_Shooter/Grenades/GrenadeDefinition_FPV.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Engine/World.h"

void UAbilityHandler_Grenade::SpawnThrown(const FVector& Location, const FRotator& Rotation)
{
	UGrenadeDefinition* const Def = Cast<UGrenadeDefinition>(GetDefinition());
	AShooterCharacter* const Caster = GetOwningCharacter();
	UWorld* const World = Caster ? Caster->GetWorld() : nullptr;
	if (!Def || !World)
	{
		return;
	}

	// A drone, not a projectile: it flies by itself. @see UGrenadeDefinition_FPV
	if (const UGrenadeDefinition_FPV* const FPV = Cast<UGrenadeDefinition_FPV>(Def))
	{
		UInventoryComponent* const DroneBag = Caster->FindComponentByClass<UInventoryComponent>();
		if (FPV->DroneClass && DroneBag && DroneBag->ConsumeGrenades(Def, 1) >= 1)
		{
			FPV->LaunchDrone(Caster, Location, Rotation);
		}
		return;
	}

	if (!Def->ProjectileClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRENADE_DEBUG] %s has no ProjectileClass"), *GetNameSafe(Def));
		return;
	}

	// Out of the bag now, at the release: the wind-up alone costs nothing.
	UInventoryComponent* const Bag = Caster->FindComponentByClass<UInventoryComponent>();
	if (!Bag || Bag->ConsumeGrenades(Def, 1) < 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRENADE_DEBUG] %s: no %s left in the bag at the release"),
			*GetNameSafe(Caster), *GetNameSafe(Def));
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Caster;
	SpawnParams.Instigator = Caster;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AGrenadeProjectile* const Grenade = World->SpawnActor<AGrenadeProjectile>(Def->ProjectileClass, Location, Rotation, SpawnParams);
	if (!Grenade)
	{
		return;
	}

	// Exactly the velocity the drawn arc was simulated with (PredictThrowPath): the thrower's own run
	// is left out, or the grenade would not land where the arc said.
	Grenade->Launch(Def, Rotation.Vector() * Def->ThrowSpeed);

	UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s threw %s from %s, %d left"),
		*GetNameSafe(Caster), *GetNameSafe(Def), *Location.ToCompactString(), Bag->GetGrenadeCount(Def));
}
