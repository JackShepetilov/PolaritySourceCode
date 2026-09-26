// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_AirDash.h"
#include "UpgradeDefinition_AirDash.h"
#include "JumpSlotParams.h"
#include "PolarityCharacter.h"
#include "ShooterCharacter.h"

UUpgrade_AirDash::UUpgrade_AirDash()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UUpgrade_AirDash::GetJumpSlotParams(FJumpSlotParams& Out) const
{
	const UUpgradeDefinition_AirDash* Def = Cast<UUpgradeDefinition_AirDash>(UpgradeDefinition);
	if (!Def)
	{
		return false;
	}

	const FAirDashLevelData& Data = Def->GetLevelData(CurrentLevel);
	Out.Mode = EJumpSlotMode::AirDash;
	Out.AirDashCharges = FMath::Max(1, Data.MaxCharges);
	Out.Cooldown = FMath::Max(0.0f, Data.CooldownSeconds);
	Out.AirDashSpeedMultiplier = FMath::Max(0.0f, Data.ImpulseMultiplier);
	return true;
}

void UUpgrade_AirDash::OnUpgradeActivated()
{
	if (APolarityCharacter* PolChar = Cast<APolarityCharacter>(GetShooterCharacter()))
	{
		PolChar->bCanAirDash = true;
	}
}

void UUpgrade_AirDash::OnUpgradeDeactivated()
{
	if (APolarityCharacter* PolChar = Cast<APolarityCharacter>(GetShooterCharacter()))
	{
		PolChar->bCanAirDash = false;
	}
}
