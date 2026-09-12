// AbilityHandler_Berserk.cpp

#include "AbilityHandler_Berserk.h"
#include "AbilityDefinition_Berserk.h"
#include "Coop/CoopPlayers.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Engine/World.h"

void UAbilityHandler_Berserk::OnActivate_Implementation()
{
	const UAbilityDefinition_Berserk* Def = Cast<UAbilityDefinition_Berserk>(GetDefinition());
	AShooterCharacter* Caster = GetOwningCharacter();
	if (!Caster || !Def)
	{
		NotifyAbilityCancelled();
		return;
	}

	const FBerserkLevelStats Stats = Def->GetStatsAtLevel(GetCurrentLevel());

	// Yourself, always. The teammate is the bonus, never the requirement: an ability that did nothing
	// when nobody was in the crosshair would be an ability the player cannot use alone, and this class
	// is the one most often out in front on its own.
	Caster->StartBerserk(Stats.Duration, Stats.FrontHalfAngle, Stats.FlankDamageMultiplier,
		Stats.HealFraction, Stats.MaxHeal);

	if (AShooterCharacter* Ally = FindAimedAlly(Stats.AllyRange, Stats.AllyAimHalfAngle))
	{
		Ally->StartBerserk(Stats.Duration, Stats.FrontHalfAngle, Stats.FlankDamageMultiplier,
			Stats.HealFraction, Stats.MaxHeal);

		UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s berserk shared with %s"),
			*GetNameSafe(Caster), *GetNameSafe(Ally));
	}

	// Nothing to wait for: there is no montage, no projectile and no window this handler owns. The
	// window belongs to the characters, and completing here is what starts the cooldown.
	NotifyAbilityComplete();
}

AShooterCharacter* UAbilityHandler_Berserk::FindAimedAlly(float Range, float AimHalfAngleDegrees) const
{
	AShooterCharacter* Caster = GetOwningCharacter();
	UWorld* World = Caster ? Caster->GetWorld() : nullptr;
	if (!World || Range <= 0.0f)
	{
		return nullptr;
	}

	// The team, not a trace. A trace would need the teammate's collision to be in the way of a line
	// the caster may well be aiming past their shoulder, and it would also happily return an enemy,
	// a prop or a wall. The question being asked is "which of my teammates am I pointing at", and the
	// list of teammates is a thing the project already keeps. @see Coop/CoopPlayers.h
	TArray<APawn*> Players;
	CoopPlayers::GetAll(World, Players);

	const FVector Eye = Caster->GetPawnViewLocation();
	const FVector Forward = Caster->GetBaseAimRotation().Vector();
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(AimHalfAngleDegrees, 1.0f, 89.0f)));
	const float RangeSq = Range * Range;

	AShooterCharacter* Best = nullptr;
	float BestDot = MinDot;

	for (APawn* Pawn : Players)
	{
		AShooterCharacter* Candidate = Cast<AShooterCharacter>(Pawn);
		if (!Candidate || Candidate == Caster)
		{
			continue;
		}

		// Nobody who cannot spend the window. A downed teammate reads as dead to IsDead() anyway --
		// their HP is zero -- and StartBerserk refuses them a second time on the far side, so a cast
		// aimed at somebody on the floor picks the next teammate in the cone instead of being eaten.
		if (Candidate->IsDead() || Candidate->IsDowned())
		{
			continue;
		}

		const FVector ToAlly = Candidate->GetActorLocation() - Eye;
		if (ToAlly.SizeSquared() > RangeSq)
		{
			continue;
		}

		const float Dot = FVector::DotProduct(Forward, ToAlly.GetSafeNormal());
		if (Dot > BestDot)
		{
			// Most centred wins, not nearest: with two teammates in the cone the one the player is
			// actually pointing at is the one they meant.
			BestDot = Dot;
			Best = Candidate;
		}
	}

	return Best;
}
