// AbilityHandler_ShieldBypass.cpp

#include "AbilityHandler_ShieldBypass.h"
#include "AbilityDefinition_ShieldBypass.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Weapons/ShieldBypassProjectile.h"
#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AShooterNPC* UAbilityHandler_ShieldBypass::FindTargetEnemy(float Range) const
{
	AShooterCharacter* Caster = GetOwningCharacter();
	UWorld* World = Caster ? Caster->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	const FVector Origin = Caster->GetPawnViewLocation();
	const FVector Aim = Caster->GetBaseAimRotation().Vector();

	// Scored on two things at once, exactly as asked: how near the middle of the screen an enemy is,
	// and how near the player. Neither alone is right -- pure screen-centre picks a distant enemy
	// over the one in your face, pure distance picks whatever you happen to be standing next to
	// regardless of where you are looking.
	AShooterNPC* Best = nullptr;
	float BestScore = -1.0f;

	for (TActorIterator<AShooterNPC> It(World); It; ++It)
	{
		AShooterNPC* Enemy = *It;
		if (!Enemy || Enemy->IsDead())
		{
			continue;
		}

		FVector ToEnemy = Enemy->GetActorLocation() - Origin;
		const float Distance = ToEnemy.Size();
		if (Distance <= KINDA_SMALL_NUMBER || Distance > Range)
		{
			continue;
		}
		ToEnemy /= Distance;

		// Behind the player is never a candidate, however close it is.
		const float Centredness = FVector::DotProduct(Aim, ToEnemy);
		if (Centredness <= 0.0f)
		{
			continue;
		}

		// Both terms in 0..1 and multiplied, so an enemy has to be reasonably central AND reasonably
		// near to win. Multiplying rather than adding means neither term can carry a candidate that
		// is hopeless on the other.
		const float Nearness = 1.0f - (Distance / Range);
		const float Score = Centredness * Nearness;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Enemy;
		}
	}

	return Best;
}

void UAbilityHandler_ShieldBypass::OnActivate_Implementation()
{
	const UAbilityDefinition_ShieldBypass* Def = Cast<UAbilityDefinition_ShieldBypass>(GetDefinition());
	AShooterCharacter* Caster = GetOwningCharacter();
	if (!Def || !Caster || !Caster->GetWorld())
	{
		NotifyAbilityCancelled();
		return;
	}

	const FShieldBypassLevelStats Stats = Def->GetStatsAtLevel(GetCurrentLevel());

	if (!Def->ProjectileClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldBypass: no ProjectileClass set on %s"),
			*GetNameSafe(Def));
		NotifyAbilityCancelled();
		return;
	}

	AShooterNPC* Target = FindTargetEnemy(Stats.Range);
	if (!Target)
	{
		// Nothing to fire at, so nothing is spent: the component only starts a cooldown when a
		// handler completes.
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldBypass: no enemy within %.0f"), Stats.Range);
		NotifyAbilityCancelled();
		return;
	}

	const FVector Muzzle = Caster->GetPawnViewLocation();
	const FRotator Facing = (Target->GetActorLocation() - Muzzle).Rotation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Caster;
	SpawnParams.Instigator = Caster;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AShieldBypassProjectile* Bolt = Caster->GetWorld()->SpawnActor<AShieldBypassProjectile>(
		Def->ProjectileClass, Muzzle, Facing, SpawnParams);

	if (!Bolt)
	{
		NotifyAbilityCancelled();
		return;
	}

	Bolt->LaunchAt(Target, Stats.ProjectileSpeed, Stats.RedirectDamageMultiplier,
		Stats.Duration, Stats.MoveSpeedMultiplier);

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldBypass: %s fired a bolt at %s (score-picked, %.0f away)"),
		*Caster->GetName(), *Target->GetName(),
		FVector::Dist(Caster->GetActorLocation(), Target->GetActorLocation()));

	NotifyAbilityComplete();
}
