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
#include "Variant_Shooter/Progression/WeaponMasteryComponent.h"
#include "Variant_Shooter/Progression/WeaponMasteryDefinition.h"
#include "Engine/World.h"

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

namespace
{
	// The mastery multiplier that answers the same question as an attachment field. Mastery is 1 or
	// less, the attachments 1 or more, and both multiply in GetAttachmentHandlingMultiplier.
	float GetMasteryValue(const FWeaponMasteryHandling& Mastery, float UWeaponAttachmentDefinition::* Field)
	{
		using A = UWeaponAttachmentDefinition;
		if (Field == &A::ADSBlendInTimeMultiplier)      { return Mastery.ADSBlendInTimeMultiplier; }
		if (Field == &A::ADSBlendOutTimeMultiplier)     { return Mastery.ADSBlendOutTimeMultiplier; }
		if (Field == &A::DrawDurationMultiplier)        { return Mastery.DrawDurationMultiplier; }
		if (Field == &A::HolsterDurationMultiplier)     { return Mastery.HolsterDurationMultiplier; }
		if (Field == &A::SprintToFireTimeMultiplier)    { return Mastery.SprintToFireTimeMultiplier; }
		if (Field == &A::ReloadDurationMultiplier)      { return Mastery.ReloadDurationMultiplier; }
		if (Field == &A::SecondaryReloadTimeMultiplier) { return Mastery.SecondaryReloadTimeMultiplier; }
		if (Field == &A::ReloadStartDurationMultiplier) { return Mastery.ReloadStartDurationMultiplier; }
		if (Field == &A::ReloadLoopDurationMultiplier)  { return Mastery.ReloadLoopDurationMultiplier; }
		if (Field == &A::ReloadEndDurationMultiplier)   { return Mastery.ReloadEndDurationMultiplier; }
		if (Field == &A::ADSFatigueRateMultiplier)      { return Mastery.ADSFatigueRateMultiplier; }
		if (Field == &A::ADSFatigueSwayMultiplier)      { return Mastery.ADSFatigueSwayMultiplier; }
		return 1.0f;
	}

	/** Holster and draw speed-up after a kill, when the gun being drawn has it. 1 otherwise. */
	float GetFastSwapSpeed(const AShooterWeapon* Incoming, const AActor* Holder)
	{
		const AShooterCharacter* const Character = Cast<AShooterCharacter>(Holder);
		const UWorld* const World = Holder ? Holder->GetWorld() : nullptr;
		if (!Incoming || !Character || !World)
		{
			return 1.0f;
		}
		const FWeaponMasteryPerks Perks = Incoming->GetMasteryPerks();
		return Perks.bFastSwapAfterKill && World->GetTimeSeconds() - Character->GetLastKillTime() <= Perks.FastSwapWindow
			? Perks.FastSwapSpeedMultiplier : 1.0f;
	}
}

float AShooterWeapon::GetAttachmentHandlingMultiplier(float UWeaponAttachmentDefinition::* Field) const
{
	float Multiplier = 1.0f;
	for (const UWeaponAttachmentDefinition* const Attachment : InstalledAttachments)
	{
		if (Attachment)
		{
			const float Value = Attachment->*Field;
			Multiplier *= FMath::IsFinite(Value) ? FMath::Clamp(Value, 1.0f, 10.0f) : 1.0f;
		}
	}

	// The holder's mastery of this gun, in the same product [author, 2026-10-16].
	return Multiplier * GetMasteryValue(GetMasteryPerks().Handling, Field);
}

FWeaponMasteryPerks AShooterWeapon::GetMasteryPerks() const
{
	if (!MasteryDefinition)
	{
		return FWeaponMasteryPerks();
	}
	const UWeaponMasteryComponent* const Mastery = UWeaponMasteryComponent::FindFor(GetOwner());
	return MasteryDefinition->Resolve(Mastery ? Mastery->GetLevel(MasteryDefinition) : 0);
}

float AShooterWeapon::GetADSFatigueRateMultiplier() const
{
	return GetAttachmentHandlingMultiplier(&UWeaponAttachmentDefinition::ADSFatigueRateMultiplier);
}

float AShooterWeapon::GetADSFatigueSwayMultiplier() const
{
	return GetAttachmentHandlingMultiplier(&UWeaponAttachmentDefinition::ADSFatigueSwayMultiplier);
}

float AShooterWeapon::GetADSBlendInTime() const
{
	return ADSBlendInTime * GetAttachmentHandlingMultiplier(&UWeaponAttachmentDefinition::ADSBlendInTimeMultiplier);
}

float AShooterWeapon::GetADSBlendOutTime() const
{
	return ADSBlendOutTime * GetAttachmentHandlingMultiplier(&UWeaponAttachmentDefinition::ADSBlendOutTimeMultiplier);
}

float AShooterWeapon::GetSprintToFireTime() const
{
	return SprintToFireTime * GetAttachmentHandlingMultiplier(&UWeaponAttachmentDefinition::SprintToFireTimeMultiplier);
}

float AShooterWeapon::GetHolsterPlayRate() const
{
	// Put away fast when the gun coming out has the fast swap after a kill.
	const AShooterCharacter* const Character = Cast<AShooterCharacter>(GetOwner());
	return GetSwitchPlayRate(HolsterMontage, HolsterDuration) * GetStockSwapSpeed()
		* GetFastSwapSpeed(Character ? Character->GetPendingWeapon() : nullptr, Character)
		/ GetAttachmentHandlingMultiplier(&UWeaponAttachmentDefinition::HolsterDurationMultiplier);
}

float AShooterWeapon::GetDrawPlayRate() const
{
	return GetSwitchPlayRate(DrawMontage, DrawDuration) * GetStockSwapSpeed() * GetFastSwapSpeed(this, GetOwner())
		/ GetAttachmentHandlingMultiplier(&UWeaponAttachmentDefinition::DrawDurationMultiplier);
}

float AShooterWeapon::GetReloadStageHandlingMultiplier(EWeaponReloadStage Stage) const
{
	switch (Stage)
	{
	case EWeaponReloadStage::Primary:
		return GetAttachmentHandlingMultiplier(&UWeaponAttachmentDefinition::ReloadDurationMultiplier);
	case EWeaponReloadStage::Secondary:
		return GetAttachmentHandlingMultiplier(&UWeaponAttachmentDefinition::SecondaryReloadTimeMultiplier);
	case EWeaponReloadStage::ShellStart:
		return GetAttachmentHandlingMultiplier(&UWeaponAttachmentDefinition::ReloadStartDurationMultiplier);
	case EWeaponReloadStage::ShellLoop:
		return GetAttachmentHandlingMultiplier(&UWeaponAttachmentDefinition::ReloadLoopDurationMultiplier);
	case EWeaponReloadStage::ShellEnd:
		return GetAttachmentHandlingMultiplier(&UWeaponAttachmentDefinition::ReloadEndDurationMultiplier);
	}
	return 1.0f;
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
