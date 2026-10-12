// DamageType_Ranged.h
// Damage type for ranged attacks

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "DamageType_Ranged.generated.h"

/**
 * Damage type used for ranged attacks (bullets, projectiles, hitscan weapons)
 * Used by ShooterDummy to determine if damage should be accepted
 */
UCLASS()
class POLARITY_API UDamageType_Ranged : public UDamageType
{
	GENERATED_BODY()

public:

	/** How hard this round bites body armour: the armour pool loses damage x this, and what is left
	 *  over for health is turned back by the same number. 1 = a rifle round. Below 1 a pistol or SMG
	 *  round, above 1 a sniper or machine gun round. Health never sees it.
	 *  @see AShooterNPC::AbsorbDamageWithShieldField */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Armor", meta = (ClampMin = "0.05"))
	float ArmorDamageScale = 1.0f;

	/** The scale for a hit of this damage type; 1 for anything that is not a ranged type. */
	static float GetArmorDamageScale(const UClass* DamageTypeClass)
	{
		if (DamageTypeClass && DamageTypeClass->IsChildOf(UDamageType_Ranged::StaticClass()))
		{
			return FMath::Max(DamageTypeClass->GetDefaultObject<UDamageType_Ranged>()->ArmorDamageScale, 0.05f);
		}
		return 1.0f;
	}
};
