// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_KickPush.h"
#include "UpgradeDefinition_MeleeKit.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/MeleeAttackComponent.h"
#include "GameFramework/Pawn.h"

void UUpgrade_KickPush::OnUpgradeActivated()
{
	if (const AShooterCharacter* const Character = GetShooterCharacter())
	{
		CachedMeleeComp = Character->FindComponentByClass<UMeleeAttackComponent>();
	}
	if (CachedMeleeComp.IsValid())
	{
		CachedMeleeComp->OnMeleeHit.AddDynamic(this, &UUpgrade_KickPush::HandleMeleeHit);
	}
}

void UUpgrade_KickPush::OnUpgradeDeactivated()
{
	if (CachedMeleeComp.IsValid())
	{
		CachedMeleeComp->OnMeleeHit.RemoveDynamic(this, &UUpgrade_KickPush::HandleMeleeHit);
	}
}

bool UUpgrade_KickPush::IsKicking() const
{
	return CachedMeleeComp.IsValid() && CachedMeleeComp->IsAttacking()
		&& CachedMeleeComp->GetCurrentAttackType() == EMeleeAttackType::Airborne;
}

float UUpgrade_KickPush::GetMeleeKnockbackDistanceMultiplier(AActor* Target) const
{
	const UUpgradeDefinition_KickPush* const Def = Cast<UUpgradeDefinition_KickPush>(UpgradeDefinition);
	return (Def && IsKicking()) ? Def->EnemyKnockbackMultiplier : 1.0f;
}

void UUpgrade_KickPush::HandleMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage)
{
	const UUpgradeDefinition_KickPush* const Def = Cast<UUpgradeDefinition_KickPush>(UpgradeDefinition);
	AShooterCharacter* const Character = GetShooterCharacter();

	// The swinger's own machine only: it is the one that predicts this body.
	if (!Def || !Character || !Character->IsLocallyControlled() || !IsKicking() || !Cast<APawn>(HitActor))
	{
		return;
	}

	// Away from the point of the hit, along the line of it. Flattened before the lift is added so a
	// kick from above does not drive the player into the sky.
	FVector Away = (Character->GetActorLocation() - HitLocation).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = -Character->GetActorForwardVector().GetSafeNormal2D();
	}
	const FVector LaunchVelocity = Away * Def->SelfPushSpeed + FVector(0.0f, 0.0f, Def->SelfPushLift);

	Character->LaunchCharacter(LaunchVelocity, true, true);
	if (!Character->HasAuthority())
	{
		Character->Server_ApplyMeleeRecoil(LaunchVelocity);
	}

	UE_LOG(LogTemp, Log, TEXT("[MELEE_DMG_DEBUG] KickPush: %s kicked off %s at %.0f cm/s"),
		*Character->GetName(), *GetNameSafe(HitActor), LaunchVelocity.Size());
}
