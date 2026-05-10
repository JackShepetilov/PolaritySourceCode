// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_AirDash.h"
#include "UpgradeDefinition_AirDash.h"
#include "PolarityCharacter.h"
#include "ShooterCharacter.h"
#include "ApexMovementComponent.h"
#include "MovementSettings.h"

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
	CaptureBaseline();
	ApplyForLevel(CurrentLevel);
}

void UUpgrade_AirDash::OnUpgradeDeactivated()
{
	if (APolarityCharacter* PolChar = Cast<APolarityCharacter>(GetShooterCharacter()))
	{
		PolChar->bCanAirDash = false;
	}
	RevertToBaseline();
}

void UUpgrade_AirDash::OnLevelChanged(int32 OldLevel, int32 NewLevel)
{
	ApplyForLevel(NewLevel);
}

void UUpgrade_AirDash::CaptureBaseline()
{
	if (bBaselineCaptured) return;

	APolarityCharacter* PolChar = Cast<APolarityCharacter>(GetShooterCharacter());
	if (!PolChar) return;
	UApexMovementComponent* Mv = PolChar->GetApexMovement();
	if (!Mv) return;
	UMovementSettings* Settings = Mv->MovementSettings;
	if (!Settings) return;

	BaselineMaxAirDashCount = Settings->MaxAirDashCount;
	BaselineAirDashCooldown = Settings->AirDashCooldown;
	BaselineAirDashSpeed    = Settings->AirDashSpeed;
	bBaselineCaptured = true;

	UE_LOG(LogTemp, Verbose, TEXT("Upgrade_AirDash: captured baseline (MaxCount=%d, Cooldown=%.2fs, Speed=%.0f)"),
		BaselineMaxAirDashCount, BaselineAirDashCooldown, BaselineAirDashSpeed);
}

void UUpgrade_AirDash::RevertToBaseline()
{
	if (!bBaselineCaptured) return;

	APolarityCharacter* PolChar = Cast<APolarityCharacter>(GetShooterCharacter());
	if (!PolChar) return;
	UApexMovementComponent* Mv = PolChar->GetApexMovement();
	if (!Mv) return;
	UMovementSettings* Settings = Mv->MovementSettings;
	if (!Settings) return;

	Settings->MaxAirDashCount = BaselineMaxAirDashCount;
	Settings->AirDashCooldown = BaselineAirDashCooldown;
	Settings->AirDashSpeed    = BaselineAirDashSpeed;
	bBaselineCaptured = false;

	UE_LOG(LogTemp, Verbose, TEXT("Upgrade_AirDash: reverted MovementSettings to baseline"));
}

void UUpgrade_AirDash::ApplyForLevel(int32 Level)
{
	const UUpgradeDefinition_AirDash* AirDashDef = Cast<UUpgradeDefinition_AirDash>(UpgradeDefinition);
	if (!AirDashDef)
	{
		UE_LOG(LogTemp, Warning, TEXT("Upgrade_AirDash: definition is not UUpgradeDefinition_AirDash subclass — per-level data not applied"));
		return;
	}

	const FAirDashLevelData& Data = AirDashDef->GetLevelData(Level);

	APolarityCharacter* PolChar = Cast<APolarityCharacter>(GetShooterCharacter());
	if (!PolChar) return;
	UApexMovementComponent* Mv = PolChar->GetApexMovement();
	if (!Mv) return;
	UMovementSettings* Settings = Mv->MovementSettings;
	if (!Settings)
	{
		UE_LOG(LogTemp, Warning, TEXT("Upgrade_AirDash: ApexMovement has no MovementSettings asset"));
		return;
	}

	// NOTE: MovementSettings is a shared DataAsset. Modifying it here mutates the asset for the
	// remainder of the session — that's why CaptureBaseline + RevertToBaseline exist. In a
	// singleplayer roguelite this is fine; multi-character coop would need a per-actor settings copy.

	Settings->MaxAirDashCount = Data.MaxCharges;
	Settings->AirDashCooldown = Data.CooldownSeconds;
	Settings->AirDashSpeed    = BaselineAirDashSpeed * Data.ImpulseMultiplier;

	// Sync runtime remaining count so the player gets the new charges immediately
	// (instead of having to land first — landing handler resets from MovementSettings).
	if (Mv->RemainingAirDashCount < Data.MaxCharges)
	{
		Mv->RemainingAirDashCount = Data.MaxCharges;
	}

	UE_LOG(LogTemp, Log, TEXT("Upgrade_AirDash: applied Lv %d (Charges=%d, Cooldown=%.2fs, Speed=%.0f)"),
		Level, Data.MaxCharges, Data.CooldownSeconds, Settings->AirDashSpeed);
}
