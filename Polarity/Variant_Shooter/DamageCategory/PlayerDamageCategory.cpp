// PlayerDamageCategory.cpp
// Damage category system implementation

#include "PlayerDamageCategory.h"
#include "Variant_Shooter/DamageTypes/DamageType_Melee.h"
#include "Variant_Shooter/DamageTypes/DamageType_Ranged.h"
#include "Variant_Shooter/DamageTypes/DamageType_Wallslam.h"
#include "Variant_Shooter/DamageTypes/DamageType_MomentumBonus.h"
#include "Variant_Shooter/DamageTypes/DamageType_Dropkick.h"
#include "Variant_Shooter/DamageTypes/DamageType_EMFProximity.h"
#include "Variant_Shooter/DamageTypes/DamageType_EMFWeapon.h"

EPlayerDamageCategory UDamageCategoryHelper::GetCategoryFromDamageType(TSubclassOf<UDamageType> DamageTypeClass)
{
	if (!DamageTypeClass)
	{
		return EPlayerDamageCategory::Base;
	}

	// Check specific types first (before checking parent classes)

	// Kinetic category - check these first as they inherit from Melee
	if (DamageTypeClass->IsChildOf(UDamageType_Wallslam::StaticClass()))
	{
		return EPlayerDamageCategory::Kinetic;
	}
	if (DamageTypeClass->IsChildOf(UDamageType_MomentumBonus::StaticClass()))
	{
		return EPlayerDamageCategory::Kinetic;
	}
	if (DamageTypeClass->IsChildOf(UDamageType_Dropkick::StaticClass()))
	{
		return EPlayerDamageCategory::Kinetic;
	}

	// EMF category - check EMFWeapon before Ranged as it inherits from it
	if (DamageTypeClass->IsChildOf(UDamageType_EMFProximity::StaticClass()))
	{
		return EPlayerDamageCategory::EMF;
	}
	if (DamageTypeClass->IsChildOf(UDamageType_EMFWeapon::StaticClass()))
	{
		return EPlayerDamageCategory::EMF;
	}

	// Base category - standard melee and ranged (checked last)
	if (DamageTypeClass->IsChildOf(UDamageType_Melee::StaticClass()))
	{
		return EPlayerDamageCategory::Base;
	}
	if (DamageTypeClass->IsChildOf(UDamageType_Ranged::StaticClass()))
	{
		return EPlayerDamageCategory::Base;
	}

	// Default to Base for any unknown damage types
	return EPlayerDamageCategory::Base;
}

FText UDamageCategoryHelper::GetCategoryDisplayName(EPlayerDamageCategory Category)
{
	switch (Category)
	{
	case EPlayerDamageCategory::Base:
		return NSLOCTEXT("DamageCategory", "Base", "Base");
	case EPlayerDamageCategory::Kinetic:
		return NSLOCTEXT("DamageCategory", "Kinetic", "Kinetic");
	case EPlayerDamageCategory::EMF:
		return NSLOCTEXT("DamageCategory", "EMF", "EMF");
	default:
		return NSLOCTEXT("DamageCategory", "Unknown", "Unknown");
	}
}
