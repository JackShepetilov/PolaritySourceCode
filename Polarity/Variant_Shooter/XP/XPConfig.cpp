// XPConfig.cpp

#include "XPConfig.h"

int32 UXPConfig::GetThresholdForLevel(int32 Level) const
{
	if (Level < 1 || Level > LevelCurve.LevelThresholds.Num()) return INT32_MAX;
	return LevelCurve.LevelThresholds[Level - 1];
}

int32 UXPConfig::GetMaxLevel() const
{
	return LevelCurve.LevelThresholds.Num();
}

int32 UXPConfig::GetBaseXPPerKill() const
{
	return LevelCurve.BaseXPPerKill;
}

float UXPConfig::GetEnemyMultiplier(TSubclassOf<AShooterNPC> EnemyClass) const
{
	if (!EnemyClass) return 1.f;
	if (const float* Found = EnemyXPMultiplier.Find(EnemyClass))
	{
		return *Found;
	}
	return 1.f;
}

bool UXPConfig::ShouldAlwaysAttributeToPlayer(TSubclassOf<UDamageType> DamageType) const
{
	if (!DamageType) return false;
	return AlwaysAttributeToPlayer.Contains(DamageType);
}
