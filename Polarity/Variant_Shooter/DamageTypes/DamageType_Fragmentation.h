// DamageType_Fragmentation.h
// Damage type for fragments: a frag grenade's blast.

#pragma once

#include "CoreMinimal.h"
#include "DamageType_Ranged.h"
#include "DamageType_Fragmentation.generated.h"

/**
 * Fragments (Docs/Grenades_Plan_2026-10-14.md §2). A bullet lands on one bone, so it has a zone: the
 * torso under the vest, a limb past it. A blast has no bone; it arrives on the whole body at once.
 * So the hit is split: ArmorBypassFraction of it is the fragments that found the open arms, legs and
 * neck and goes straight to health, and the rest goes into the armour like any round.
 *
 * An armour that covers the whole body (no CoveredBones: a drone's energy shield) has nothing open to
 * find and takes the whole hit. Light class: a tank's hull plate stops it.
 */
UCLASS()
class POLARITY_API UDamageType_Fragmentation : public UDamageType_Ranged
{
	GENERATED_BODY()

public:

	/** The share of a hit that goes past body armour, straight to health. 0 = all into the armour. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Armor", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ArmorBypassFraction = 0.4f;

	/** The share for a hit of this damage type; 0 for anything that is not fragmentation. */
	static float GetArmorBypassFraction(const UClass* DamageTypeClass)
	{
		if (DamageTypeClass && DamageTypeClass->IsChildOf(UDamageType_Fragmentation::StaticClass()))
		{
			return FMath::Clamp(DamageTypeClass->GetDefaultObject<UDamageType_Fragmentation>()->ArmorBypassFraction, 0.0f, 1.0f);
		}
		return 0.0f;
	}
};
