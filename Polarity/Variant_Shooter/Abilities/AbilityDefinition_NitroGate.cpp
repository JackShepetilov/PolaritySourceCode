// AbilityDefinition_NitroGate.cpp

#include "AbilityDefinition_NitroGate.h"
#include "NitroGate.h"
#include "GameFramework/ProjectileMovementComponent.h"

FNitroGateLevelStats UAbilityDefinition_NitroGate::GetStatsAtLevel(int32 Level) const
{
	if (Levels.Num() == 0)
	{
		return FNitroGateLevelStats{};
	}
	return Levels[FMath::Clamp(Level - 1, 0, Levels.Num() - 1)];
}

float UAbilityDefinition_NitroGate::GetThrowGravityScale() const
{
	if (const ANitroGate* Defaults = GateClass ? GateClass.GetDefaultObject() : nullptr)
	{
		if (const UProjectileMovementComponent* Flight = Defaults->FindComponentByClass<UProjectileMovementComponent>())
		{
			return Flight->ProjectileGravityScale;
		}
	}
	return 1.0f;
}

FAbilityCommonStats UAbilityDefinition_NitroGate::GetCommonStatsAtLevel(int32 Level) const
{
	const FNitroGateLevelStats Stats = GetStatsAtLevel(Level);
	FAbilityCommonStats Common;
	Common.Cooldown = Stats.Cooldown;
	Common.MinimumChargeToActivate = Stats.MinimumChargeToActivate;
	return Common;
}
