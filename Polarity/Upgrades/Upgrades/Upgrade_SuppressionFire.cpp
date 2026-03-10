// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_SuppressionFire.h"
#include "UpgradeDefinition_SuppressionFire.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "ShooterNPC.h"
#include "AIAccuracyComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

UUpgrade_SuppressionFire::UUpgrade_SuppressionFire()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgrade_SuppressionFire::OnUpgradeActivated()
{
	DefSuppression = Cast<UUpgradeDefinition_SuppressionFire>(UpgradeDefinition);
	if (!DefSuppression.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Suppression Fire: UpgradeDefinition is not UUpgradeDefinition_SuppressionFire!"));
	}
}

void UUpgrade_SuppressionFire::OnOwnerDealtDamage(AActor* Target, float Damage, bool bKilled)
{
	if (!DefSuppression.IsValid() || !Target)
	{
		return;
	}

	// Only works with hitscan weapons
	AShooterWeapon* Weapon = GetCurrentWeapon();
	if (!Weapon || !Weapon->IsHitscan())
	{
		return;
	}

	// Only affects ShooterNPC and subclasses (FlyingDrone, SniperTurret, etc.)
	AShooterNPC* NPC = Cast<AShooterNPC>(Target);
	if (!NPC)
	{
		return;
	}

	// Get player speed
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	float PlayerSpeed = 0.0f;
	if (UCharacterMovementComponent* MovementComp = Character->GetCharacterMovement())
	{
		PlayerSpeed = MovementComp->Velocity.Size();
	}
	else
	{
		PlayerSpeed = Character->GetVelocity().Size();
	}

	// Below minimum speed: no effect
	if (PlayerSpeed < DefSuppression->MinSpeedThreshold)
	{
		return;
	}

	// Calculate suppression duration proportional to speed
	const float SpeedRange = DefSuppression->MaxSpeedForFullEffect - DefSuppression->MinSpeedThreshold;
	const float SpeedFactor = (SpeedRange > KINDA_SMALL_NUMBER)
		? FMath::Clamp((PlayerSpeed - DefSuppression->MinSpeedThreshold) / SpeedRange, 0.0f, 1.0f)
		: 1.0f;

	const float Duration = FMath::Lerp(DefSuppression->MinSuppressionDuration, DefSuppression->MaxSuppressionDuration, SpeedFactor);

	// Find accuracy component on the NPC
	UAIAccuracyComponent* AccuracyComp = NPC->FindComponentByClass<UAIAccuracyComponent>();
	if (!AccuracyComp)
	{
		return;
	}

	// Apply suppression with configured diminishing returns
	AccuracyComp->ApplySuppression(Duration, DefSuppression->DiminishingReturnsFactor);

	UE_LOG(LogTemp, Log, TEXT("Suppression Fire: Applied %.2fs suppression to %s (player speed: %.0f cm/s, factor: %.2f)"),
		Duration, *NPC->GetName(), PlayerSpeed, SpeedFactor);
}
