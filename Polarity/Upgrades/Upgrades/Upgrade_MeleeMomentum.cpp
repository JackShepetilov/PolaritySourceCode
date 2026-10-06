// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_MeleeMomentum.h"
#include "UpgradeDefinition_MeleeKit.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/MeleeAttackComponent.h"

float UUpgrade_MeleeMomentum::GetMeleeDamageMultiplier(AActor* Target) const
{
	const UUpgradeDefinition_MeleeMomentum* const Def = Cast<UUpgradeDefinition_MeleeMomentum>(UpgradeDefinition);
	const AShooterCharacter* const Character = GetShooterCharacter();
	if (!Def || !Character || !Target)
	{
		return 1.0f;
	}

	// The speed the swing BEGAN with when it is the fist: the lunge that follows is the melee's own
	// doing and must not pay for itself. A melee weapon has no such record, so it reads the body.
	FVector Velocity = Character->GetVelocity();
	if (const UMeleeAttackComponent* const Melee = Character->FindComponentByClass<UMeleeAttackComponent>())
	{
		if (Melee->IsAttacking())
		{
			Velocity = Melee->GetOwnerVelocityAtAttackStart();
		}
	}

	const FVector ToTarget = (Target->GetActorLocation() - Character->GetActorLocation()).GetSafeNormal();
	const float Closing = FVector::DotProduct(Velocity, ToTarget);
	const float Range = FMath::Max(1.0f, Def->FullSpeed - Def->MinSpeed);
	const float Alpha = FMath::Clamp((Closing - Def->MinSpeed) / Range, 0.0f, 1.0f);
	const float Multiplier = FMath::Lerp(1.0f, Def->MaxMultiplier, Alpha);

	if (Alpha > 0.0f)
	{
		UE_LOG(LogTemp, Log, TEXT("[MELEE_DMG_DEBUG] Momentum: closing %.0f cm/s on %s -> x%.2f"),
			Closing, *Target->GetName(), Multiplier);
	}
	return Multiplier;
}
