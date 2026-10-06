// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_MeleeCloak.h"
#include "UpgradeDefinition_MeleeKit.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/MeleeAttackComponent.h"
#include "GameFramework/Pawn.h"

UUpgrade_MeleeCloak::UUpgrade_MeleeCloak()
{
	// Watches for the weapon coming out while cloaked. One comparison a frame.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

float UUpgrade_MeleeCloak::GetCloakDuration() const
{
	const UUpgradeDefinition_MeleeCloak* const Def = Cast<UUpgradeDefinition_MeleeCloak>(UpgradeDefinition);
	return Def ? Def->CloakDuration : 0.0f;
}

void UUpgrade_MeleeCloak::OnUpgradeActivated()
{
	AShooterCharacter* const Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	Character->OnMeleeChargeHoldStarted.AddDynamic(this, &UUpgrade_MeleeCloak::HandleHoldStarted);
	Character->OnMeleeChargeHoldReleased.AddDynamic(this, &UUpgrade_MeleeCloak::HandleHoldReleased);

	CachedMeleeComp = Character->FindComponentByClass<UMeleeAttackComponent>();
	if (CachedMeleeComp.IsValid())
	{
		CachedMeleeComp->OnMeleeAttackStarted.AddDynamic(this, &UUpgrade_MeleeCloak::HandleAttackStarted);
		CachedMeleeComp->OnMeleeAttackEnded.AddDynamic(this, &UUpgrade_MeleeCloak::HandleAttackEnded);
		CachedMeleeComp->OnMeleeHit.AddDynamic(this, &UUpgrade_MeleeCloak::HandleMeleeHit);
	}
}

void UUpgrade_MeleeCloak::OnUpgradeDeactivated()
{
	if (AShooterCharacter* const Character = GetShooterCharacter())
	{
		Character->OnMeleeChargeHoldStarted.RemoveDynamic(this, &UUpgrade_MeleeCloak::HandleHoldStarted);
		Character->OnMeleeChargeHoldReleased.RemoveDynamic(this, &UUpgrade_MeleeCloak::HandleHoldReleased);
		if (Character->HasAuthority())
		{
			Character->SetCloaked(false);
		}
	}
	if (CachedMeleeComp.IsValid())
	{
		CachedMeleeComp->OnMeleeAttackStarted.RemoveDynamic(this, &UUpgrade_MeleeCloak::HandleAttackStarted);
		CachedMeleeComp->OnMeleeAttackEnded.RemoveDynamic(this, &UUpgrade_MeleeCloak::HandleAttackEnded);
		CachedMeleeComp->OnMeleeHit.RemoveDynamic(this, &UUpgrade_MeleeCloak::HandleMeleeHit);
	}
}

void UUpgrade_MeleeCloak::HandleHoldStarted()
{
	bKeyHeld = true;
}

void UUpgrade_MeleeCloak::HandleHoldReleased()
{
	bKeyHeld = false;
}

void UUpgrade_MeleeCloak::HandleAttackStarted()
{
	AShooterCharacter* const Character = GetShooterCharacter();
	if (!Character || !Character->IsLocallyControlled() || !Character->IsCloaked())
	{
		return;
	}

	// Swinging gives the cloak up, and this swing is the one that only shoves.
	bSwingFromCloak = true;
	Character->Server_EndMeleeCloak();
}

void UUpgrade_MeleeCloak::HandleAttackEnded()
{
	bSwingFromCloak = false;
}

void UUpgrade_MeleeCloak::HandleMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage)
{
	AShooterCharacter* const Character = GetShooterCharacter();
	if (!Character || !Character->IsLocallyControlled() || !bKeyHeld || bSwingFromCloak || !Cast<APawn>(HitActor))
	{
		return;
	}

	// Here for the swinger's own screen, and on the server through the request, because the end of
	// the swing is decided on each machine separately.
	Character->KeepHandsEmptyAfterMelee();
	Character->Server_RequestMeleeCloak();
}

float UUpgrade_MeleeCloak::GetMeleeDamageMultiplier(AActor* Target) const
{
	return bSwingFromCloak ? 0.0f : 1.0f;
}

void UUpgrade_MeleeCloak::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AShooterCharacter* const Character = GetShooterCharacter();
	if (!Character || !Character->IsLocallyControlled() || !Character->IsCloaked())
	{
		bEndRequested = false;
		return;
	}

	// A weapon in the hands, or on its way into them, ends it. The swing's own stow is neither: the
	// cloak can arrive while the swing that earned it is still finishing.
	const EWeaponSwitchPhase HandsPhase = Character->GetWeaponSwitchPhase();
	const bool bHandsEmpty = Character->IsWeaponHolsteredByPlayer() || HandsPhase == EWeaponSwitchPhase::StowedForMelee;
	const bool bSwinging = CachedMeleeComp.IsValid() && CachedMeleeComp->IsAttacking();
	if (!bSwinging && !bHandsEmpty && !bEndRequested)
	{
		bEndRequested = true;
		Character->Server_EndMeleeCloak();
	}
}
