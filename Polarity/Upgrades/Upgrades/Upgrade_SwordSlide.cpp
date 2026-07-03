// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_SwordSlide.h"
#include "ApexMovementComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

void UUpgrade_SwordSlide::OnUpgradeActivated()
{
	CachedDef = Cast<UUpgradeDefinition_SwordSlide>(UpgradeDefinition);
	if (!CachedDef.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[SWORD_SLIDE] Activation failed: definition is not UUpgradeDefinition_SwordSlide"));
		return;
	}

	BindMovement();
	ApplySlideOverrideIfEligible();

	UE_LOG(LogTemp, Warning, TEXT("[SWORD_SLIDE] Activated Lv%d/%d"),
		CurrentLevel, CachedDef->MaxLevel);
}

void UUpgrade_SwordSlide::OnUpgradeDeactivated()
{
	ClearSlideOverride();
	CachedMovement.Reset();

	UE_LOG(LogTemp, Warning, TEXT("[SWORD_SLIDE] Deactivated"));
}

void UUpgrade_SwordSlide::OnLevelChanged(int32 OldLevel, int32 NewLevel)
{
	ClearSlideOverride();
	ApplySlideOverrideIfEligible();

	UE_LOG(LogTemp, Warning, TEXT("[SWORD_SLIDE] Level %d -> %d"), OldLevel, NewLevel);
}

void UUpgrade_SwordSlide::OnWeaponChanged(AShooterWeapon* /*OldWeapon*/, AShooterWeapon* /*NewWeapon*/)
{
	ClearSlideOverride();
	ApplySlideOverrideIfEligible();
}

float UUpgrade_SwordSlide::GetMeleeDamageMultiplier(AActor* /*Target*/) const
{
	if (!IsSlidingWithEligibleWeapon())
	{
		return 1.0f;
	}

	return GetCurrentLevelData().SlidingDamageMultiplier;
}

void UUpgrade_SwordSlide::BindMovement()
{
	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		CachedMovement = Cast<UApexMovementComponent>(Character->GetCharacterMovement());
	}
}

void UUpgrade_SwordSlide::ApplySlideOverrideIfEligible()
{
	UApexMovementComponent* Movement = CachedMovement.Get();
	if (!Movement || !IsEligibleWeapon(GetCurrentWeapon()))
	{
		return;
	}

	const FSwordSlideLevelData& Data = GetCurrentLevelData();
	Movement->SetExternalSlideSpeedBurstOverride(Data.SlideMinSpeedBurst, Data.SlideMaxSpeedBurst);
	bAppliedSlideOverride = true;

	UE_LOG(LogTemp, Warning, TEXT("[SWORD_SLIDE] Applied slide burst override min=%.1f max=%.1f"),
		Data.SlideMinSpeedBurst, Data.SlideMaxSpeedBurst);
}

void UUpgrade_SwordSlide::ClearSlideOverride()
{
	if (UApexMovementComponent* Movement = CachedMovement.Get())
	{
		if (bAppliedSlideOverride)
		{
			Movement->ClearExternalSlideSpeedBurstOverride();
		}
	}

	bAppliedSlideOverride = false;
}

bool UUpgrade_SwordSlide::IsEligibleWeapon(const AShooterWeapon* Weapon) const
{
	if (!Weapon || !Weapon->IsMeleeWeapon())
	{
		return false;
	}

	const FSwordSlideLevelData& Data = GetCurrentLevelData();
	return !Data.RequiredWeaponClass || Weapon->IsA(Data.RequiredWeaponClass);
}

bool UUpgrade_SwordSlide::IsSlidingWithEligibleWeapon() const
{
	const UApexMovementComponent* Movement = CachedMovement.Get();
	return Movement
		&& Movement->IsSliding()
		&& IsEligibleWeapon(GetCurrentWeapon());
}

const FSwordSlideLevelData& UUpgrade_SwordSlide::GetCurrentLevelData() const
{
	if (const UUpgradeDefinition_SwordSlide* Def = CachedDef.Get())
	{
		return Def->GetLevelData(CurrentLevel);
	}

	static const FSwordSlideLevelData DefaultData;
	return DefaultData;
}
