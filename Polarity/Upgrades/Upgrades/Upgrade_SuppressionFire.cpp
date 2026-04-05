// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_SuppressionFire.h"
#include "UpgradeDefinition_SuppressionFire.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "ShooterNPC.h"
#include "MeleeNPC.h"
#include "KamikazeDroneNPC.h"
#include "SniperTurretNPC.h"
#include "AIAccuracyComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"

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

	// Exclude enemies that don't use ranged accuracy: melee fighters, kamikaze drones, turrets
	if (NPC->IsA<AMeleeNPC>() || NPC->IsA<AKamikazeDroneNPC>() || NPC->IsA<ASniperTurretNPC>())
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

	// Remember whether suppression was already active before we apply it
	const bool bWasAlreadySuppressed = AccuracyComp->IsSuppressed();

	// Apply suppression with configured diminishing returns
	AccuracyComp->ApplySuppression(Duration, DefSuppression->DiminishingReturnsFactor);

	// Play Plot Armor sound only on fresh application, not on extension
	if (DefSuppression->SuppressionSound && !bWasAlreadySuppressed)
	{
		const float Volume = FMath::Lerp(DefSuppression->SoundVolumeMin, DefSuppression->SoundVolumeMax, SpeedFactor);
		const float Pitch  = FMath::Lerp(DefSuppression->SoundPitchMin,  DefSuppression->SoundPitchMax,  SpeedFactor);
		const float Reverb = FMath::Lerp(DefSuppression->SoundReverbMin, DefSuppression->SoundReverbMax, SpeedFactor);

		if (UAudioComponent* AudioComp = UGameplayStatics::SpawnSoundAttached(
			DefSuppression->SuppressionSound,
			NPC->GetRootComponent(),
			NAME_None,
			FVector::ZeroVector,
			EAttachLocation::KeepRelativeOffset,
			/*bStopWhenAttachedToDestroyed=*/ true,
			Volume,
			Pitch))
		{
			// ReverbSend is a named float parameter in the Sound Cue.
			// Add a "Float Parameter" node named "ReverbSend" and route it to the reverb submix send level.
			AudioComp->SetFloatParameter(FName("ReverbSend"), Reverb);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Suppression Fire: Applied %.2fs suppression to %s (player speed: %.0f cm/s, factor: %.2f)"),
		Duration, *NPC->GetName(), PlayerSpeed, SpeedFactor);
}
