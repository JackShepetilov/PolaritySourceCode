// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_AirDash.h"
#include "PolarityCharacter.h"
#include "ShooterCharacter.h"

UUpgrade_AirDash::UUpgrade_AirDash()
{
	PrimaryComponentTick.bCanEverTick = false;
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
