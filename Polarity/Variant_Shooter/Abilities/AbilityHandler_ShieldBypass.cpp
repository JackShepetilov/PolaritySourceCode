// AbilityHandler_ShieldBypass.cpp

#include "AbilityHandler_ShieldBypass.h"
#include "AbilityDefinition_ShieldBypass.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

AShooterNPC* UAbilityHandler_ShieldBypass::FindTargetEnemy(float Range) const
{
	AShooterCharacter* Caster = GetOwningCharacter();
	if (!Caster || !Caster->GetWorld())
	{
		return nullptr;
	}

	// Same origin and direction the weapon shoots from (GetBaseAimRotation / GetPawnViewLocation),
	// so what the ability hits is what the crosshair is on. Taking the camera transform instead
	// would drift from the gun on any character whose body is not facing where it aims.
	const FVector Start = Caster->GetPawnViewLocation();
	const FVector End = Start + Caster->GetBaseAimRotation().Vector() * Range;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Caster);

	// A sphere rather than a line: this picks a target for a support ability, and demanding
	// pixel-accurate aim for something that does no damage is friction with no upside.
	FHitResult Hit;
	const bool bHit = Caster->GetWorld()->SweepSingleByChannel(
		Hit, Start, End, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(60.0f), Params);

	if (!bHit)
	{
		return nullptr;
	}

	AShooterNPC* Enemy = Cast<AShooterNPC>(Hit.GetActor());
	if (!Enemy || Enemy->IsDead())
	{
		return nullptr;
	}
	return Enemy;
}

void UAbilityHandler_ShieldBypass::OnActivate_Implementation()
{
	const UAbilityDefinition_ShieldBypass* Def = Cast<UAbilityDefinition_ShieldBypass>(GetDefinition());
	if (!Def)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldBypass: definition is not a UAbilityDefinition_ShieldBypass"));
		NotifyAbilityCancelled();
		return;
	}

	const FShieldBypassLevelStats Stats = Def->GetStatsAtLevel(GetCurrentLevel());

	AShooterNPC* Target = FindTargetEnemy(Stats.Range);
	if (!Target)
	{
		// Cancelled rather than completed: nothing happened, so nothing should go on cooldown. The
		// component only starts the cooldown on completion, which is exactly the distinction wanted
		// here -- a miss must not cost the ability.
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldBypass: no enemy under the crosshair within %.0f"),
			Stats.Range);
		NotifyAbilityCancelled();
		return;
	}

	Target->ApplyShieldBypass(Stats.Duration, Stats.MoveSpeedMultiplier, Stats.RedirectDamageMultiplier);

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldBypass: %s opened for %.1fs by %s"),
		*Target->GetName(), Stats.Duration, *GetNameSafe(GetOwningCharacter()));

	// Instant: no cast to wait on, so the cooldown starts now.
	NotifyAbilityComplete();
}
