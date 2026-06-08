// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_FullHealthBonus.h"
#include "UpgradeDefinition_FullHealthBonus.h"
#include "ShooterCharacter.h"
#include "PolarityCharacter.h"
#include "ApexMovementComponent.h"
#include "Curves/CurveFloat.h"

UUpgrade_FullHealthBonus::UUpgrade_FullHealthBonus()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgrade_FullHealthBonus::OnUpgradeActivated()
{
	CachedDef = Cast<UUpgradeDefinition_FullHealthBonus>(UpgradeDefinition);
	if (!CachedDef.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[FULLHP_DEBUG] Activation FAILED — UpgradeDefinition is not UUpgradeDefinition_FullHealthBonus!"));
		return;
	}

	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		Character->OnDamaged.AddDynamic(this, &UUpgrade_FullHealthBonus::HandleHealthChanged);
	}

	const FFullHealthBonusLevelData& LD = CachedDef->GetLevelData(CurrentLevel);
	UE_LOG(LogTemp, Warning, TEXT("[FULLHP_DEBUG] ACTIVATED Lv%d — Threshold=%.0f%% MaxSpeed=x%.2f MaxMelee=x%.2f"),
		CurrentLevel, LD.HealthThreshold * 100.0f, LD.MaxSpeedMultiplier, LD.MaxMeleeDamageMultiplier);

	RefreshSpeedMultiplier();
}

void UUpgrade_FullHealthBonus::OnUpgradeDeactivated()
{
	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		Character->OnDamaged.RemoveDynamic(this, &UUpgrade_FullHealthBonus::HandleHealthChanged);

		if (APolarityCharacter* PolChar = Cast<APolarityCharacter>(Character))
		{
			if (UApexMovementComponent* Mv = PolChar->GetApexMovement())
			{
				Mv->ExternalSpeedMultiplier = 1.0f;
			}
		}
	}
}

void UUpgrade_FullHealthBonus::HandleHealthChanged(float /*LifePercent*/, float /*ArmorPercent*/)
{
	RefreshSpeedMultiplier();
}

float UUpgrade_FullHealthBonus::ComputeStrength() const
{
	if (!CachedDef.IsValid())
	{
		return 0.0f;
	}

	const AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return 0.0f;
	}

	const float MaxHP = Character->GetMaxHP();
	if (MaxHP <= 0.0f)
	{
		return 0.0f;
	}

	const float Pct = Character->GetCurrentHP() / MaxHP;
	const FFullHealthBonusLevelData& LD = CachedDef->GetLevelData(CurrentLevel);

	const float Range = 1.0f - LD.HealthThreshold;
	if (Range <= KINDA_SMALL_NUMBER)
	{
		// Degenerate threshold (>= 100%): simple on/off at full HP.
		return (Pct >= LD.HealthThreshold) ? 1.0f : 0.0f;
	}

	const float T = FMath::Clamp((Pct - LD.HealthThreshold) / Range, 0.0f, 1.0f);
	return CachedDef->ScalingCurve
		? FMath::Clamp(CachedDef->ScalingCurve->GetFloatValue(T), 0.0f, 1.0f)
		: T;
}

void UUpgrade_FullHealthBonus::RefreshSpeedMultiplier()
{
	if (!CachedDef.IsValid())
	{
		return;
	}

	APolarityCharacter* PolChar = Cast<APolarityCharacter>(GetShooterCharacter());
	if (!PolChar)
	{
		return;
	}
	UApexMovementComponent* Mv = PolChar->GetApexMovement();
	if (!Mv)
	{
		return;
	}

	const float Strength = ComputeStrength();
	const FFullHealthBonusLevelData& LD = CachedDef->GetLevelData(CurrentLevel);
	Mv->ExternalSpeedMultiplier = FMath::Lerp(1.0f, LD.MaxSpeedMultiplier, Strength);
}

float UUpgrade_FullHealthBonus::GetMeleeDamageMultiplier(AActor* Target) const
{
	if (!CachedDef.IsValid())
	{
		return 1.0f;
	}

	const float Strength = ComputeStrength();
	const FFullHealthBonusLevelData& LD = CachedDef->GetLevelData(CurrentLevel);
	return FMath::Lerp(1.0f, LD.MaxMeleeDamageMultiplier, Strength);
}
