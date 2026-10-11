// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_KickPush.h"
#include "UpgradeDefinition_MeleeKit.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/MeleeAttackComponent.h"
#include "GameFramework/Pawn.h"

void UUpgrade_KickPush::OnUpgradeActivated()
{
	const AShooterCharacter* const Character = GetShooterCharacter();
	CachedMeleeComp = Character ? Character->FindComponentByClass<UMeleeAttackComponent>() : nullptr;
}

float UUpgrade_KickPush::GetMeleeSelfKnockbackScale(AActor* Target) const
{
	const UUpgradeDefinition_KickPush* const Def = Cast<UUpgradeDefinition_KickPush>(UpgradeDefinition);

	// A kick is the swing made in the air, and only a kick into somebody shoves back.
	const bool bKicking = CachedMeleeComp.IsValid() && CachedMeleeComp->IsAttacking()
		&& CachedMeleeComp->GetCurrentAttackType() == EMeleeAttackType::Airborne;
	return (Def && bKicking && Cast<APawn>(Target)) ? Def->SelfKnockbackScale : 0.0f;
}
