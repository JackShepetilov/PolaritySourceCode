// WeaponDropSettings.cpp

#include "Variant_Shooter/Weapons/WeaponDropSettings.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"

UClass* UWeaponDropSettings::GetDropActorClass()
{
	const UWeaponDropSettings* const Settings = GetDefault<UWeaponDropSettings>();
	if (Settings && !Settings->DropActorClass.IsNull())
	{
		if (UClass* const Loaded = Settings->DropActorClass.LoadSynchronous())
		{
			return Loaded;
		}
		UE_LOG(LogTemp, Warning, TEXT("[WEAPON_DROP] DropActorClass %s did not load, using the C++ class"),
			*Settings->DropActorClass.ToString());
	}
	return ADroppedRangedWeapon::StaticClass();
}
