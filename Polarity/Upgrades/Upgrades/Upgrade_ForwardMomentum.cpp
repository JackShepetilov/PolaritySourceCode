// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_ForwardMomentum.h"
#include "UpgradeDefinition_ForwardMomentum.h"
#include "ShooterCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

UUpgrade_ForwardMomentum::UUpgrade_ForwardMomentum()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgrade_ForwardMomentum::OnUpgradeActivated()
{
	DefMomentum = Cast<UUpgradeDefinition_ForwardMomentum>(UpgradeDefinition);
	if (!DefMomentum.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Forward Momentum: UpgradeDefinition is not UUpgradeDefinition_ForwardMomentum!"));
	}
}

float UUpgrade_ForwardMomentum::GetDamageMultiplier(AActor* Target) const
{
	if (!DefMomentum.IsValid() || !Target)
	{
		LastMomentumMultiplier = 1.0f;
		return 1.0f;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		LastMomentumMultiplier = 1.0f;
		return 1.0f;
	}

	UCharacterMovementComponent* MovementComp = Character->GetCharacterMovement();
	if (!MovementComp)
	{
		LastMomentumMultiplier = 1.0f;
		return 1.0f;
	}

	// Get horizontal velocity only (ignore vertical movement)
	FVector Velocity = MovementComp->Velocity;
	Velocity.Z = 0.0f;
	float Speed = Velocity.Size();

	// Below minimum speed threshold: no modifier
	if (Speed < DefMomentum->MinSpeedThreshold)
	{
		LastMomentumMultiplier = 1.0f;
		return 1.0f;
	}

	// Direction from player to target (horizontal only)
	FVector ToTarget = Target->GetActorLocation() - Character->GetActorLocation();
	ToTarget.Z = 0.0f;

	if (ToTarget.IsNearlyZero())
	{
		LastMomentumMultiplier = 1.0f;
		return 1.0f;
	}

	// Dot product of normalized velocity with direction to target
	// +1.0 = moving directly toward, -1.0 = moving directly away, 0 = perpendicular
	FVector VelocityDir = Velocity.GetSafeNormal();
	FVector TargetDir = ToTarget.GetSafeNormal();
	float ApproachDot = FVector::DotProduct(VelocityDir, TargetDir);

	// Speed factor: 0 at MinSpeed, 1 at MaxSpeed
	float SpeedRange = DefMomentum->MaxSpeedForFullEffect - DefMomentum->MinSpeedThreshold;
	float SpeedFactor = (SpeedRange > KINDA_SMALL_NUMBER)
		? FMath::Clamp((Speed - DefMomentum->MinSpeedThreshold) / SpeedRange, 0.0f, 1.0f)
		: 1.0f;

	// Combined approach factor scaled by speed
	float ScaledDot = ApproachDot * SpeedFactor;

	// Apply bonus or penalty
	float Multiplier;
	if (ScaledDot > 0.0f)
	{
		// Moving toward target: bonus
		Multiplier = 1.0f + (ScaledDot * DefMomentum->ForwardBonusMultiplier);
	}
	else
	{
		// Moving away from target: penalty (ScaledDot is negative, so this subtracts)
		Multiplier = 1.0f + (ScaledDot * DefMomentum->BackwardPenaltyMultiplier);
	}

	// Clamp to prevent negative damage
	Multiplier = FMath::Max(Multiplier, 0.0f);

	LastMomentumMultiplier = Multiplier;

	UE_LOG(LogTemp, Verbose, TEXT("Forward Momentum: Dot=%.2f Speed=%.0f Factor=%.2f -> Mult=%.2f"),
		ApproachDot, Speed, SpeedFactor, Multiplier);

	return Multiplier;
}
