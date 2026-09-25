// Fill out your copyright notice in the Description page of Project Settings.

#include "EnemyCombatProfile.h"

void UEnemyCombatProfile::PostLoad()
{
	Super::PostLoad();

	// The old switch defaulted to on, and only a value that differs from the default is saved, so a
	// false here can only have come from an asset that had it turned off. That one means "never".
	if (!bCrouchWhenPeeking_DEPRECATED)
	{
		CrouchPeekChance = 0.0f;
		bCrouchWhenPeeking_DEPRECATED = true;
	}
}
