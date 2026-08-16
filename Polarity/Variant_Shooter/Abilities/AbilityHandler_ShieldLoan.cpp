// AbilityHandler_ShieldLoan.cpp

#include "AbilityHandler_ShieldLoan.h"
#include "AbilityDefinition_ShieldLoan.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "EMFVelocityModifier.h"
#include "Engine/World.h"

AShooterNPC* UAbilityHandler_ShieldLoan::FindTargetEnemy(float Range) const
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

void UAbilityHandler_ShieldLoan::OnActivate_Implementation()
{
	const UAbilityDefinition_ShieldLoan* Def = Cast<UAbilityDefinition_ShieldLoan>(GetDefinition());
	AShooterCharacter* Caster = GetOwningCharacter();
	if (!Def || !Caster)
	{
		NotifyAbilityCancelled();
		return;
	}

	const FShieldLoanLevelStats Stats = Def->GetStatsAtLevel(GetCurrentLevel());

	AShooterNPC* Target = FindTargetEnemy(Stats.Range);
	if (!Target)
	{
		// Nothing to borrow from, so nothing is spent: the component only starts a cooldown when a
		// handler completes.
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldLoan: no enemy within %.0f"), Stats.Range);
		NotifyAbilityCancelled();
		return;
	}

	// The loan is a slice of what is actually on the enemy right now, so it is worth more against
	// somebody the team has already been working on.
	float AvailableShield = 0.0f;
	if (const UEMFVelocityModifier* Modifier = Target->FindComponentByClass<UEMFVelocityModifier>())
	{
		AvailableShield = FMath::Abs(Modifier->GetCharge());
	}

	const float Pledged = AvailableShield * FMath::Clamp(Stats.LoanFraction, 0.0f, 1.0f);
	if (Pledged <= KINDA_SMALL_NUMBER)
	{
		// A bare enemy has nothing to lend. Refusing here rather than dashing for free keeps the
		// ability honest: the movement is paid for by the debt, not granted alongside it.
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldLoan: %s has no shield to lend"), *Target->GetName());
		NotifyAbilityCancelled();
		return;
	}

	Target->AddShieldLoan(Pledged);

	// The dash the loan buys. Toward the target, flattened: this is an approach, not a leap.
	FVector ToTarget = Target->GetActorLocation() - Caster->GetActorLocation();
	ToTarget.Z = 0.0f;
	if (ToTarget.Normalize())
	{
		const float Speed = FMath::Min(Pledged * Stats.DashSpeedPerPledged, Stats.MaxDashSpeed);
		const FVector LaunchVelocity = ToTarget * Speed;

		// Launched on the authority AND on the caster's own machine. A server-only launch is a
		// server arguing with a client that is still predicting its own movement, which is exactly
		// the stutter the melee shove had before it was split this way.
		Caster->LaunchCharacter(LaunchVelocity, true, false);
		if (!Caster->IsLocallyControlled())
		{
			Caster->Client_ApplyKnockback(LaunchVelocity);
		}

		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldLoan: %s pledged %.1f, %s dashes at %.0f"),
			*Target->GetName(), Pledged, *Caster->GetName(), Speed);
	}

	NotifyAbilityComplete();
}
