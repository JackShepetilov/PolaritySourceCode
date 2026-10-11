// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_MeleeCloak.h"
#include "UpgradeDefinition_MeleeKit.h"
#include "MeleeHoldAttack.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/MeleeAttackComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"

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

bool UUpgrade_MeleeCloak::GetMeleeHoldAttack(FMeleeHoldAttack& Out) const
{
	const UUpgradeDefinition_MeleeCloak* const Def = Cast<UUpgradeDefinition_MeleeCloak>(UpgradeDefinition);
	if (!Def)
	{
		return false;
	}

	Out.HoldTime = Def->HoldTime;
	Out.Animation = Def->HoldAttackAnimation.AttackMontage ? &Def->HoldAttackAnimation : nullptr;
	Out.Label = Def->DisplayName;
	return true;
}

void UUpgrade_MeleeCloak::OnUpgradeActivated()
{
	const AShooterCharacter* const Character = GetShooterCharacter();
	CachedMeleeComp = Character ? Character->FindComponentByClass<UMeleeAttackComponent>() : nullptr;
	if (CachedMeleeComp.IsValid())
	{
		CachedMeleeComp->OnMeleeAttackStarted.AddDynamic(this, &UUpgrade_MeleeCloak::HandleAttackStarted);
		CachedMeleeComp->OnMeleeAttackEnded.AddDynamic(this, &UUpgrade_MeleeCloak::HandleAttackEnded);
		CachedMeleeComp->OnMeleeHit.AddDynamic(this, &UUpgrade_MeleeCloak::HandleMeleeHit);
	}
}

void UUpgrade_MeleeCloak::OnUpgradeDeactivated()
{
	RestoreMeleeMaterials();

	if (AShooterCharacter* const Character = GetShooterCharacter())
	{
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
	RestoreMeleeMaterials();
}

void UUpgrade_MeleeCloak::HandleMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage)
{
	AShooterCharacter* const Character = GetShooterCharacter();
	if (!Character || !Character->IsLocallyControlled() || bSwingFromCloak || !Cast<APawn>(HitActor)
		|| !CachedMeleeComp.IsValid() || !CachedMeleeComp->IsHoldAttack())
	{
		return;
	}

	// Here for the swinger's own screen, and on the server through the request, because the end of
	// the swing is decided on each machine separately.
	Character->KeepHandsEmptyAfterMelee();
	Character->Server_RequestMeleeCloak();
	ApplyCloakMaterial();
}

void UUpgrade_MeleeCloak::ApplyCloakMaterial()
{
	const UUpgradeDefinition_MeleeCloak* const Def = Cast<UUpgradeDefinition_MeleeCloak>(UpgradeDefinition);
	const AShooterCharacter* const Character = GetShooterCharacter();
	USkeletalMeshComponent* const Mesh = Character ? Character->GetMeleeMesh() : nullptr;
	if (!Def || !Def->CloakMeleeMaterial || !Mesh || SavedMeleeMaterials.Num() > 0)
	{
		return;
	}

	const int32 Count = Mesh->GetNumMaterials();
	SavedMeleeMaterials.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		SavedMeleeMaterials.Add(Mesh->GetMaterial(Index));
		Mesh->SetMaterial(Index, Def->CloakMeleeMaterial);
	}
}

void UUpgrade_MeleeCloak::RestoreMeleeMaterials()
{
	if (SavedMeleeMaterials.Num() == 0)
	{
		return;
	}

	const AShooterCharacter* const Character = GetShooterCharacter();
	if (USkeletalMeshComponent* const Mesh = Character ? Character->GetMeleeMesh() : nullptr)
	{
		for (int32 Index = 0; Index < SavedMeleeMaterials.Num() && Index < Mesh->GetNumMaterials(); ++Index)
		{
			Mesh->SetMaterial(Index, SavedMeleeMaterials[Index]);
		}
	}
	SavedMeleeMaterials.Reset();
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
	// cloak can arrive while the swing that earned it is still finishing. A weapon merely flying in
	// on the hook does not count either; it ends the cloak when it is equipped.
	const EWeaponSwitchPhase HandsPhase = Character->GetWeaponSwitchPhase();
	const bool bWeaponOut = Character->GetCurrentWeapon() != nullptr
		&& (HandsPhase == EWeaponSwitchPhase::None || HandsPhase == EWeaponSwitchPhase::Drawing);
	const bool bSwinging = CachedMeleeComp.IsValid() && CachedMeleeComp->IsAttacking();
	if (!bSwinging && bWeaponOut && !bEndRequested)
	{
		bEndRequested = true;
		Character->Server_EndMeleeCloak();
	}
}
