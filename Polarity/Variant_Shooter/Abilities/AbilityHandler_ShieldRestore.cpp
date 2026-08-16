// AbilityHandler_ShieldRestore.cpp

#include "AbilityHandler_ShieldRestore.h"
#include "AbilityDefinition_ShieldRestore.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "EMFVelocityModifier.h"
#include "Engine/World.h"

AShooterNPC* UAbilityHandler_ShieldRestore::FindTargetEnemy(float Range) const
{
	AShooterCharacter* Caster = GetOwningCharacter();
	if (!Caster || !Caster->GetWorld())
	{
		return nullptr;
	}

	const FVector Start = Caster->GetPawnViewLocation();
	const FVector End = Start + Caster->GetBaseAimRotation().Vector() * Range;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Caster);

	FHitResult Hit;
	const bool bHit = Caster->GetWorld()->SweepSingleByChannel(
		Hit, Start, End, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(60.0f), Params);
	if (!bHit)
	{
		return nullptr;
	}

	AShooterNPC* Enemy = Cast<AShooterNPC>(Hit.GetActor());
	return (Enemy && !Enemy->IsDead()) ? Enemy : nullptr;
}

void UAbilityHandler_ShieldRestore::OnActivate_Implementation()
{
	const UAbilityDefinition_ShieldRestore* Def = Cast<UAbilityDefinition_ShieldRestore>(GetDefinition());
	AShooterCharacter* Caster = GetOwningCharacter();
	if (!Def || !Caster)
	{
		NotifyAbilityCancelled();
		return;
	}

	const FShieldRestoreLevelStats Stats = Def->GetStatsAtLevel(GetCurrentLevel());

	AShooterNPC* Target = FindTargetEnemy(Stats.Range);
	if (!Target)
	{
		NotifyAbilityCancelled();
		return;
	}

	UEMFVelocityModifier* Modifier = Target->FindComponentByClass<UEMFVelocityModifier>();
	if (!Modifier)
	{
		NotifyAbilityCancelled();
		return;
	}

	// Shield is the inverse of charge, so giving it back means pulling the magnitude toward zero.
	// You can only hand back what was taken, which is what makes an untouched enemy worth nothing
	// and a nearly stripped one worth everything -- no separate bookkeeping needed to create the
	// incentive the design asks for.
	const float Current = Modifier->GetCharge();
	const float Restorable = FMath::Min(FMath::Abs(Current), Stats.MaxShieldRestored);
	if (Restorable <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldRestore: %s has a full shield, nothing to give back"),
			*Target->GetName());
		NotifyAbilityCancelled();
		return;
	}

	Modifier->SetCharge(FMath::Sign(Current) * (FMath::Abs(Current) - Restorable));
	Caster->RestoreHealth(Restorable * Stats.HealPerShieldRestored);

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldRestore: gave %s back %.1f shield, healed %s for %.1f"),
		*Target->GetName(), Restorable, *Caster->GetName(), Restorable * Stats.HealPerShieldRestored);

	NotifyAbilityComplete();
}
