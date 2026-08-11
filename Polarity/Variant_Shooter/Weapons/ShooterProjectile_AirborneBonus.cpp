// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterProjectile_AirborneBonus.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

AShooterProjectile_AirborneBonus::AShooterProjectile_AirborneBonus()
{
}

float AShooterProjectile_AirborneBonus::GetProjectileDamageMultiplier(AActor* Target) const
{
	if (Target == GetOwner() || Target == GetInstigator())
	{
		return 1.0f;
	}

	return IsAirborneTarget(Target) ? AirborneDamageMultiplier : 1.0f;
}

bool AShooterProjectile_AirborneBonus::IsAirborneTarget(const AActor* Target) const
{
	const ACharacter* Character = Cast<ACharacter>(Target);
	if (!Character)
	{
		return false;
	}

	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Movement)
	{
		return false;
	}

	return Movement->IsFalling()
		|| (bTreatFlyingMovementModeAsAirborne && Movement->MovementMode == MOVE_Flying);
}
