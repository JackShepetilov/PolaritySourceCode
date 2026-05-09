// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_DropKick.h"
#include "ShooterCharacter.h"
#include "MeleeAttackComponent.h"

UUpgrade_DropKick::UUpgrade_DropKick()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgrade_DropKick::OnUpgradeActivated()
{
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	if (UMeleeAttackComponent* MeleeComp = Character->FindComponentByClass<UMeleeAttackComponent>())
	{
		MeleeComp->bDropKickUnlocked = true;
	}
}

void UUpgrade_DropKick::OnUpgradeDeactivated()
{
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	if (UMeleeAttackComponent* MeleeComp = Character->FindComponentByClass<UMeleeAttackComponent>())
	{
		MeleeComp->bDropKickUnlocked = false;
	}
}
