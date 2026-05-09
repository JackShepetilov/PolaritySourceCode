// AbilityDefinition_Burst.cpp

#include "AbilityDefinition_Burst.h"
#include "Animation/AnimMontage.h"

UAbilityDefinition_Burst::UAbilityDefinition_Burst()
{
	// Seed with one level so designers immediately see fields to fill in.
	Levels.Add(FBurstLevelStats{});
}

FAbilityCommonStats UAbilityDefinition_Burst::GetCommonStatsAtLevel(int32 Level) const
{
	const FBurstLevelStats Stats = GetBurstStatsAtLevel(Level);
	FAbilityCommonStats Common;
	Common.Cooldown = Stats.Cooldown;
	Common.MinimumChargeToActivate = Stats.MinimumChargeToActivate;
	return Common;
}

FBurstLevelStats UAbilityDefinition_Burst::GetBurstStatsAtLevel(int32 Level) const
{
	if (Levels.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("AbilityDefinition_Burst '%s' has empty Levels — returning defaults"), *GetName());
		return FBurstLevelStats{};
	}
	const int32 ClampedIndex = FMath::Clamp(Level - 1, 0, Levels.Num() - 1);
	return Levels[ClampedIndex];
}

TSubclassOf<AEMFProjectile> UAbilityDefinition_Burst::GetProjectileClassAtLevel(int32 Level) const
{
	const FBurstLevelStats Stats = GetBurstStatsAtLevel(Level);
	return Stats.ProjectileClassOverride ? Stats.ProjectileClassOverride : DefaultProjectileClass;
}

UAnimMontage* UAbilityDefinition_Burst::PickRandomLoopMontage() const
{
	if (LoopMontages.Num() == 0)
	{
		return nullptr;
	}

	float TotalWeight = 0.0f;
	for (const FWeightedAnimMontage& Entry : LoopMontages)
	{
		if (Entry.Montage && Entry.Weight > 0.0f)
		{
			TotalWeight += Entry.Weight;
		}
	}
	if (TotalWeight <= 0.0f)
	{
		return nullptr;
	}

	const float Roll = FMath::FRandRange(0.0f, TotalWeight);
	float Accumulator = 0.0f;
	for (const FWeightedAnimMontage& Entry : LoopMontages)
	{
		if (!Entry.Montage || Entry.Weight <= 0.0f)
		{
			continue;
		}
		Accumulator += Entry.Weight;
		if (Roll <= Accumulator)
		{
			return Entry.Montage;
		}
	}
	for (int32 i = LoopMontages.Num() - 1; i >= 0; --i)
	{
		if (LoopMontages[i].Montage && LoopMontages[i].Weight > 0.0f)
		{
			return LoopMontages[i].Montage;
		}
	}
	return nullptr;
}
