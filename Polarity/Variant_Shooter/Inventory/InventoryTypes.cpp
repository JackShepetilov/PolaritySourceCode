// InventoryTypes.cpp
// The one thing in FInventorySlot that cannot live in the header.
//
// TSubclassOf<AShooterWeapon> compares through its conversion to UClass*, and that conversion
// checks the class against AShooterWeapon::StaticClass(). That needs the COMPLETE type, which the
// header deliberately does not have: InventoryTypes.h is included by widgets, pickups and the
// component, and dragging the whole weapon header into all of them to serve one comparison is a
// bad trade. So the comparison lives here, where including it costs nothing.

#include "Variant_Shooter/Inventory/InventoryTypes.h"

#include "Variant_Shooter/Pickups/InventoryPickup.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

bool FInventorySlot::operator==(const FInventorySlot& Other) const
{
	return Kind == Other.Kind
		&& Payload == Other.Payload
		&& Count == Other.Count
		&& StackMax == Other.StackMax
		&& bInstalled == Other.bInstalled
		&& InstalledOnWeapon == Other.InstalledOnWeapon
		&& PickupClass == Other.PickupClass;
}
