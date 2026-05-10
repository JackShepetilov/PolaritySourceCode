// XPConfig.cpp

#include "XPConfig.h"

int32 UXPConfig::GetThresholdForLevel(ESkillCategory Category, int32 Level) const
{
	const FSkillCurve* Curve = SkillCurves.Find(Category);
	if (!Curve) return INT32_MAX;
	if (Level < 1 || Level > Curve->LevelThresholds.Num()) return INT32_MAX;
	return Curve->LevelThresholds[Level - 1];
}

int32 UXPConfig::GetMaxLevel(ESkillCategory Category) const
{
	const FSkillCurve* Curve = SkillCurves.Find(Category);
	return Curve ? Curve->LevelThresholds.Num() : 0;
}

int32 UXPConfig::GetBaseXPPerKill(ESkillCategory Category) const
{
	const FSkillCurve* Curve = SkillCurves.Find(Category);
	return Curve ? Curve->BaseXPPerKill : 0;
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

bool UXPConfig::GetSkillForDamageType(TSubclassOf<UDamageType> DamageType, ESkillCategory& OutCategory) const
{
	if (!DamageType) return false;
	if (const ESkillCategory* Found = KillXPRouting.Find(DamageType))
	{
		OutCategory = *Found;
		return true;
	}
	return false;
}

bool UXPConfig::ShouldAlwaysAttributeToPlayer(TSubclassOf<UDamageType> DamageType) const
{
	if (!DamageType) return false;
	return AlwaysAttributeToPlayer.Contains(DamageType);
}

const FMovementActionConfig* UXPConfig::GetMovementConfig(EMovementAction Action) const
{
	return MovementActions.Find(Action);
}
