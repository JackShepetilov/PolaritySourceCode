// WeaponDropSettings.h
// Project Settings -> Polarity -> Weapon Drops: the one actor every gun on the floor is made of.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "WeaponDropSettings.generated.h"

class ADroppedRangedWeapon;

/**
 * A gun on the floor used to be its own blueprint, one BP_Dropped_<Gun> per weapon, each with a
 * static mesh and a WeaponClass typed in by hand. Now a drop builds its look from the weapon class
 * it carries (ADroppedRangedWeapon::SpawnFor), so the game needs exactly one drop actor, and this is
 * where it is named. What differs between drops is the gun; what the drop does (pickup sound, stun,
 * where the pull flies to) is the same for all of them and lives on this one class.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Weapon Drops"))
class POLARITY_API UWeaponDropSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	virtual FName GetCategoryName() const override { return FName("Polarity"); }

	/** The drop actor spawned for every gun. Empty = the bare C++ class, which works but has no
	 *  pickup sound or tuned pull. */
	UPROPERTY(EditAnywhere, config, Category = "Drop")
	TSoftClassPtr<ADroppedRangedWeapon> DropActorClass;

	/** DropActorClass, loaded, or the C++ class when it is empty or fails to load. Never null. */
	static UClass* GetDropActorClass();
};
