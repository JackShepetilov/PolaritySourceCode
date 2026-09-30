// Upgrade_SlideFireFriction.cpp

#include "Upgrade_SlideFireFriction.h"
#include "UpgradeDefinition_SlideFireFriction.h"
#include "ApexMovementComponent.h"
#include "TimerManager.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

UUpgrade_SlideFireFriction::UUpgrade_SlideFireFriction()
{
	PrimaryComponentTick.bCanEverTick = false;
}

float UUpgrade_SlideFireFriction::GetSlideFireFrictionScale() const
{
	const UUpgradeDefinition_SlideFireFriction* Def = Cast<UUpgradeDefinition_SlideFireFriction>(UpgradeDefinition);
	return Def ? FMath::Clamp(Def->GetLevelData(CurrentLevel).FrictionScale, 0.0f, 1.0f) : 1.0f;
}

void UUpgrade_SlideFireFriction::OnUpgradeActivated()
{
	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		CachedMovement = Cast<UApexMovementComponent>(Character->GetCharacterMovement());
	}
	UE_LOG(LogTemp, Log, TEXT("[SLIDE_SLOT] Slide Fire Friction activated Lv%d"), CurrentLevel);
}

void UUpgrade_SlideFireFriction::OnUpgradeDeactivated()
{
	EndStreak();
	CachedMovement.Reset();
}

void UUpgrade_SlideFireFriction::OnWeaponFired()
{
	// The streak is the owning client's call. The server hears it in the move flags, and running this
	// on the server as well would have two writers for one bit.
	const AShooterCharacter* Character = GetShooterCharacter();
	UApexMovementComponent* Movement = CachedMovement.Get();
	const AShooterWeapon* Weapon = GetCurrentWeapon();
	const UUpgradeDefinition_SlideFireFriction* Def = Cast<UUpgradeDefinition_SlideFireFriction>(UpgradeDefinition);
	if (!Character || !Character->IsLocallyControlled() || !Movement || !Weapon || Weapon->IsMeleeWeapon() || !Def)
	{
		return;
	}

	const float Window = Weapon->GetActualRefireRate() * (1.0f + Def->GetLevelData(CurrentLevel).RefireSlack);
	if (Window <= 0.0f)
	{
		return;
	}

	if (!Movement->IsSlideFireStreak())
	{
		UE_LOG(LogTemp, Log, TEXT("[SLIDE_SLOT] fire streak on, window %.3f s"), Window);
	}
	Movement->SetSlideFireStreak(true);

	// Restarted by every shot: the streak ends one window after the LAST shot.
	GetWorld()->GetTimerManager().SetTimer(StreakTimer, this, &UUpgrade_SlideFireFriction::EndStreak, Window, false);
}

void UUpgrade_SlideFireFriction::EndStreak()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StreakTimer);
	}
	if (UApexMovementComponent* Movement = CachedMovement.Get())
	{
		if (Movement->IsSlideFireStreak())
		{
			UE_LOG(LogTemp, Log, TEXT("[SLIDE_SLOT] fire streak off"));
		}
		Movement->SetSlideFireStreak(false);
	}
}
