// ShooterWeapon_Stock.cpp
// The stock attachment: handling multipliers. Docs/Stock_Attachment_Plan_2026-10-01.md.
//
// Nothing is applied or cached. Each funnel asks for its multiplier at the moment it needs it:
// switch play rates (GetDrawPlayRate / GetHolsterPlayRate), the ADS alpha interp
// (AShooterCharacter::UpdateADS), the reload montages and timers, and the hip spread
// (GetCurrentSpreadDegrees). The installed attachment list replicates, so every machine reads the
// same number and the third person montages others see run at the owner's speed.

#include "ShooterWeapon.h"
#include "WeaponAttachmentDefinition.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Upgrades/UpgradeManagerComponent.h"

namespace
{
	const UWeaponAttachmentDefinition* GetStock(const AShooterWeapon* Weapon)
	{
		return Weapon->GetAttachmentOfType(EWeaponAttachmentType::Stock);
	}

	// A speed of zero would freeze a montage and divide a timer by zero.
	float SafeSpeed(float Speed)
	{
		return FMath::Max(Speed, 0.1f);
	}
}

float AShooterWeapon::GetStockSwapSpeed() const
{
	const UWeaponAttachmentDefinition* const Stock = GetStock(this);
	return Stock ? SafeSpeed(Stock->SwapSpeedMultiplier) : 1.0f;
}

float AShooterWeapon::GetStockADSSpeed() const
{
	const UWeaponAttachmentDefinition* const Stock = GetStock(this);
	return Stock ? SafeSpeed(Stock->ADSSpeedMultiplier) : 1.0f;
}

float AShooterWeapon::GetStockReloadSpeed() const
{
	const UWeaponAttachmentDefinition* const Stock = GetStock(this);
	float Speed = Stock ? SafeSpeed(Stock->ReloadSpeedMultiplier) : 1.0f;

	// The holder's upgrades ride on the same number, because this is the one place every reload
	// timing, montage rate and sound pitch already reads. The name is the stock's for history.
	// TODO(COOP): the buff is timed per machine, so a client and the server can disagree on the
	// rate for a reload that straddles its end.
	if (const AShooterCharacter* const Holder = Cast<AShooterCharacter>(PawnOwner))
	{
		if (const UUpgradeManagerComponent* const Upgrades = Holder->GetUpgradeManager())
		{
			Speed *= SafeSpeed(Upgrades->GetCombinedReloadSpeedMultiplier());
		}
	}
	return Speed;
}

float AShooterWeapon::GetStockHipSpreadFactor() const
{
	const UWeaponAttachmentDefinition* const Stock = GetStock(this);
	if (!Stock)
	{
		return 1.0f;
	}

	// Hip only: the factor fades out over the same time the sight comes up, so aimed spread stays
	// whatever the weapon and its AdsMultiplier say.
	const float Hip = FMath::Max(Stock->HipSpreadMultiplier, 0.0f);
	if (const AShooterCharacter* ShooterOwner = Cast<AShooterCharacter>(PawnOwner))
	{
		const float ADSAlpha = FMath::Clamp(ShooterOwner->GetADSAlpha(), 0.0f, 1.0f);
		return FMath::Lerp(Hip, 1.0f, ADSAlpha);
	}
	return Hip;
}
