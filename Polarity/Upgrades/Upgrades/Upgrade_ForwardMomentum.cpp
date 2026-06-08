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
		UE_LOG(LogTemp, Error, TEXT("[FORWARD_DEBUG] Activation FAILED — UpgradeDefinition is not UUpgradeDefinition_ForwardMomentum!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[FORWARD_DEBUG] ACTIVATED — Fwd=+%.0f%% Back=-%.0f%% MinSpd=%.0f MaxSpd=%.0f (buffs BOTH hitscan and melee)"),
		DefMomentum->ForwardBonusMultiplier * 100.0f,
		DefMomentum->BackwardPenaltyMultiplier * 100.0f,
		DefMomentum->MinSpeedThreshold,
		DefMomentum->MaxSpeedForFullEffect);
}

float UUpgrade_ForwardMomentum::GetDamageMultiplier(AActor* Target) const
{
	// Hitscan path — applies to damaging hitscan guns (e.g. yanked enemy weapons).
	// The starting wave pistol does 0 damage, so this is a no-op there by design.
	const float Mult = ComputeMomentumMultiplier(Target);
	UE_LOG(LogTemp, Warning, TEXT("[FORWARD_DEBUG] HITSCAN  -> mult=%.2f vs %s"), Mult, *GetNameSafe(Target));
	return Mult;
}

float UUpgrade_ForwardMomentum::GetMeleeDamageMultiplier(AActor* Target) const
{
	// Melee path — applies to BOTH the fist (MeleeAttackComponent) and the melee weapon
	// (ShooterWeapon_Melee), since both query GetCombinedMeleeDamageMultiplier. Scales the
	// base melee damage only (the momentum/dropkick bonus components are applied separately).
	const float Mult = ComputeMomentumMultiplier(Target);
	UE_LOG(LogTemp, Warning, TEXT("[FORWARD_DEBUG] MELEE    -> mult=%.2f vs %s"), Mult, *GetNameSafe(Target));
	return Mult;
}

float UUpgrade_ForwardMomentum::ComputeMomentumMultiplier(AActor* Target) const
{
	if (!DefMomentum.IsValid() || !Target)
	{
		LastMomentumMultiplier = 1.0f;
		UE_LOG(LogTemp, Warning, TEXT("[FORWARD_DEBUG]   mult=1.00 (no definition or no target)"));
		return 1.0f;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		LastMomentumMultiplier = 1.0f;
		UE_LOG(LogTemp, Warning, TEXT("[FORWARD_DEBUG]   mult=1.00 (no owning character)"));
		return 1.0f;
	}

	UCharacterMovementComponent* MovementComp = Character->GetCharacterMovement();
	if (!MovementComp)
	{
		LastMomentumMultiplier = 1.0f;
		return 1.0f;
	}

	// Horizontal velocity only (ignore vertical movement).
	FVector Velocity = MovementComp->Velocity;
	Velocity.Z = 0.0f;
	const float Speed = Velocity.Size();

	// Below the minimum speed threshold: no modifier.
	if (Speed < DefMomentum->MinSpeedThreshold)
	{
		LastMomentumMultiplier = 1.0f;
		UE_LOG(LogTemp, Warning, TEXT("[FORWARD_DEBUG]   mult=1.00 (speed %.0f < MinSpeed %.0f)"),
			Speed, DefMomentum->MinSpeedThreshold);
		return 1.0f;
	}

	// Direction from player to target (horizontal only).
	FVector ToTarget = Target->GetActorLocation() - Character->GetActorLocation();
	ToTarget.Z = 0.0f;
	if (ToTarget.IsNearlyZero())
	{
		LastMomentumMultiplier = 1.0f;
		UE_LOG(LogTemp, Warning, TEXT("[FORWARD_DEBUG]   mult=1.00 (target coincident with player)"));
		return 1.0f;
	}

	// Dot of normalized velocity with direction to target: +1 toward, -1 away, 0 perpendicular.
	const FVector VelocityDir = Velocity.GetSafeNormal();
	const FVector TargetDir = ToTarget.GetSafeNormal();
	const float ApproachDot = FVector::DotProduct(VelocityDir, TargetDir);

	// Speed factor: 0 at MinSpeed, 1 at MaxSpeedForFullEffect.
	const float SpeedRange = DefMomentum->MaxSpeedForFullEffect - DefMomentum->MinSpeedThreshold;
	const float SpeedFactor = (SpeedRange > KINDA_SMALL_NUMBER)
		? FMath::Clamp((Speed - DefMomentum->MinSpeedThreshold) / SpeedRange, 0.0f, 1.0f)
		: 1.0f;

	const float ScaledDot = ApproachDot * SpeedFactor;

	float Multiplier;
	if (ScaledDot > 0.0f)
	{
		// Moving toward the target: bonus.
		Multiplier = 1.0f + (ScaledDot * DefMomentum->ForwardBonusMultiplier);
	}
	else
	{
		// Moving away from the target: penalty (ScaledDot is negative, so this subtracts).
		Multiplier = 1.0f + (ScaledDot * DefMomentum->BackwardPenaltyMultiplier);
	}

	Multiplier = FMath::Max(Multiplier, 0.0f);
	LastMomentumMultiplier = Multiplier;

	UE_LOG(LogTemp, Warning, TEXT("[FORWARD_DEBUG]   Dot=%.2f Speed=%.0f Factor=%.2f -> mult=%.2f"),
		ApproachDot, Speed, SpeedFactor, Multiplier);

	return Multiplier;
}
