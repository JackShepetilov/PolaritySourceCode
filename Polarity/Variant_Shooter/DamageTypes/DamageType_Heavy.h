// DamageType_Heavy.h
// Damage type for heavy ordnance: rockets, anti-tank shells

#pragma once

#include "CoreMinimal.h"
#include "DamageType_Ranged.h"
#include "DamageType_Heavy.generated.h"

/**
 * The heavy damage class (Docs/Damage_Model_Rework_Plan_2026-10-03.md §2.3): what a hit zone that
 * only takes heavy damage (a tank's hull plate) lets through. Everything else is light. Set it as the
 * damage type of a rocket's projectile or of a heavy weapon; children count as heavy too.
 */
UCLASS()
class POLARITY_API UDamageType_Heavy : public UDamageType_Ranged
{
	GENERATED_BODY()
};
