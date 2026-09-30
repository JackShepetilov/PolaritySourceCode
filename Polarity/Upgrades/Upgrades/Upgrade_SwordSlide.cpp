// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_SwordSlide.h"
#include "UpgradeDefinition_SwordSlide.h"
#include "ApexMovementComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

UUpgrade_SwordSlide::UUpgrade_SwordSlide()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgrade_SwordSlide::OnUpgradeActivated()
{
	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		CachedMovement = Cast<UApexMovementComponent>(Character->GetCharacterMovement());
	}

	if (UApexMovementComponent* Movement = CachedMovement.Get())
	{
		Movement->OnSlideStarted.AddUniqueDynamic(this, &UUpgrade_SwordSlide::HandleSlideStarted);
	}

	ReserveShotsThisSlide = 0;
	UE_LOG(LogTemp, Log, TEXT("[SLIDE_SLOT] Sliding Shooter activated Lv%d"), CurrentLevel);
}

void UUpgrade_SwordSlide::OnUpgradeDeactivated()
{
	if (UApexMovementComponent* Movement = CachedMovement.Get())
	{
		Movement->OnSlideStarted.RemoveDynamic(this, &UUpgrade_SwordSlide::HandleSlideStarted);
	}
	CachedMovement.Reset();
}

void UUpgrade_SwordSlide::HandleSlideStarted()
{
	ReserveShotsThisSlide = 0;
}

bool UUpgrade_SwordSlide::TryTakeShotFromReserve(const AShooterWeapon* Weapon)
{
	const UUpgradeDefinition_SwordSlide* Def = Cast<UUpgradeDefinition_SwordSlide>(UpgradeDefinition);
	const UApexMovementComponent* Movement = CachedMovement.Get();
	if (!Def || !Weapon || !Movement || !Movement->IsSliding())
	{
		return false;
	}

	const FSwordSlideLevelData& Data = Def->GetLevelData(CurrentLevel);
	if (Movement->Velocity.Size2D() < Data.MinSlideSpeed)
	{
		return false;
	}

	const int32 Allowance = FMath::Max(1, FMath::FloorToInt(Weapon->GetMagazineSize() * Data.MagazineFraction));
	if (ReserveShotsThisSlide >= Allowance)
	{
		return false;
	}

	++ReserveShotsThisSlide;
	UE_LOG(LogTemp, Verbose, TEXT("[SLIDE_SLOT] Sliding Shooter: shot %d/%d from the reserve"), ReserveShotsThisSlide, Allowance);
	return true;
}
