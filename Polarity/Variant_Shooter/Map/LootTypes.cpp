// LootTypes.cpp

#include "Variant_Shooter/Map/LootTypes.h"

#include "Variant_Shooter/Pickups/AmmoPickup.h"
#include "Variant_Shooter/Pickups/CurrencyPickup.h"
#include "Variant_Shooter/Pickups/HealthPickup.h"
#include "Variant_Shooter/Pickups/ArmorPickup.h"

namespace PolarityLoot
{
	ELootBand BandForValue(float Value)
	{
		// The only four numbers in the system, and they exist for the eye rather than the maths.
		// Moved here and not into an asset because a colour scale that different maps disagree
		// about is a colour scale a player cannot learn.
		if (Value >= 0.85f) { return ELootBand::Legendary; }
		if (Value >= 0.60f) { return ELootBand::Epic; }
		if (Value >= 0.30f) { return ELootBand::Rare; }
		return ELootBand::Common;
	}

	bool ApplyAmountScale(AActor* Pickup, float Scale)
	{
		if (!Pickup || FMath::IsNearlyEqual(Scale, 1.0f))
		{
			return false;
		}

		// Every pickup in the project keeps its size in its own field with its own name, and there
		// is no shared base to hang a virtual on: health and armour are plain actors, ammo and
		// money are inventory pickups. Four casts is the honest version of that, and it fails
		// loudly-by-doing-nothing for a class with nothing to scale, which is what an attachment is.
		if (AAmmoPickup* Ammo = Cast<AAmmoPickup>(Pickup))
		{
			const int32 Base = Ammo->Rounds;
			// Zero rounds means "one cell's worth" and the pickup resolves that itself later, so
			// there is nothing here to multiply: scaling it would turn the default into a number.
			if (Base > 0)
			{
				Ammo->Rounds = FMath::Max(1, FMath::RoundToInt(Base * Scale));
				return true;
			}
			return false;
		}

		if (ACurrencyPickup* Money = Cast<ACurrencyPickup>(Pickup))
		{
			Money->Amount = FMath::Max(1, FMath::RoundToInt(Money->Amount * Scale));
			return true;
		}

		if (AHealthPickup* Health = Cast<AHealthPickup>(Pickup))
		{
			Health->HealAmount = FMath::Max(1.0f, Health->HealAmount * Scale);
			return true;
		}

		if (AArmorPickup* Armor = Cast<AArmorPickup>(Pickup))
		{
			Armor->ArmorAmount = FMath::Max(1.0f, Armor->ArmorAmount * Scale);
			return true;
		}

		return false;
	}
}
