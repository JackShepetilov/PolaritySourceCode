// AbilityHandler_SniperPassive.cpp

#include "AbilityHandler_SniperPassive.h"
#include "AbilityDefinition_SniperPassive.h"
#include "AbilityComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Engine/World.h"

static TAutoConsoleVariable<int32> CVarSniperPassiveDebug(
	TEXT("polarity.sniper.passivedebug"),
	0,
	TEXT("Log the Sniper passive's accumulator and the damage it pays out.\n")
	TEXT("Answers one question: is the ground being counted, and is the shot spending it.\n")
	TEXT("  0 = off\n")
	TEXT("  1 = log every shot, on every machine that sees one"),
	ECVF_Default);

void UAbilityHandler_SniperPassive::OnEquip_Implementation()
{
	TravelSinceLastShot = 0.0f;
	TravelAtLastShot = 0.0f;
	bHasLastLocation = false;
	EngagedTargets.Reset();

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] SniperPassive equipped on %s (counting=%d)"),
		*GetNameSafe(GetOwningCharacter()), ShouldAccumulateHere() ? 1 : 0);
}

void UAbilityHandler_SniperPassive::OnUnequip_Implementation()
{
	EngagedTargets.Reset();
}

bool UAbilityHandler_SniperPassive::ShouldAccumulateHere() const
{
	const AShooterCharacter* Character = GetOwningCharacter();
	if (!Character)
	{
		return false;
	}

	// The authority applies the damage from its own count, and the owning client needs its own count
	// to draw the readout. A simulated proxy of somebody else's Sniper is asked for neither.
	return Character->IsLocallyControlled() || Character->HasAuthority();
}

void UAbilityHandler_SniperPassive::OnPassiveTick(float DeltaTime)
{
	AShooterCharacter* Character = GetOwningCharacter();
	if (!Character || !ShouldAccumulateHere())
	{
		return;
	}

	const FVector Now = Character->GetActorLocation();
	if (!bHasLastLocation)
	{
		LastOwnerLocation = Now;
		bHasLastLocation = true;
		return;
	}

	const float Step = FVector::Dist(LastOwnerLocation, Now);
	LastOwnerLocation = Now;

	const UAbilityDefinition_SniperPassive* Def = Cast<UAbilityDefinition_SniperPassive>(GetDefinition());
	const float Full = Def ? FMath::Max(1.0f, Def->GetStatsAtLevel(GetCurrentLevel()).DistanceForFullBonus) : 4000.0f;

	// Clamped as it goes in rather than when it is read: an unclamped accumulator would keep a long
	// walk between fights on the books and hand the player a full-power shot they did not earn in
	// the fight they are actually in.
	TravelSinceLastShot = FMath::Min(TravelSinceLastShot + Step, Full);
}

void UAbilityHandler_SniperPassive::OnOwnerFiredWeapon()
{
	if (!ShouldAccumulateHere())
	{
		return;
	}

	// The whole shot is paid for here, once, BEFORE any of its damage is worked out. Every pellet of
	// a shotgun blast therefore carries the same value, and the accumulator is empty from this
	// instant on -- hit or miss, because the mechanic is per trigger pull and paying out only on
	// hits would make it a second accuracy bonus on top of the one the weapon already is.
	TravelAtLastShot = TravelSinceLastShot;
	TravelSinceLastShot = 0.0f;

	if (CVarSniperPassiveDebug.GetValueOnAnyThread() > 0)
	{
		const AShooterCharacter* Character = GetOwningCharacter();
		UE_LOG(LogTemp, Warning,
			TEXT("[SNIPER_DEBUG] shot latched role=%d own=%d travel=%.0f -> pierce=%.1f"),
			Character ? (int32)Character->GetLocalRole() : -1,
			Character && Character->IsLocallyControlled() ? 1 : 0,
			TravelAtLastShot, PierceDamageForTravel(TravelAtLastShot));
	}
}

float UAbilityHandler_SniperPassive::PierceDamageForTravel(float Travel) const
{
	const UAbilityDefinition_SniperPassive* Def = Cast<UAbilityDefinition_SniperPassive>(GetDefinition());
	if (!Def)
	{
		return 0.0f;
	}

	const FSniperPassiveLevelStats Stats = Def->GetStatsAtLevel(GetCurrentLevel());
	const float Fraction = FMath::Clamp(Travel / FMath::Max(1.0f, Stats.DistanceForFullBonus), 0.0f, 1.0f);

	// Clamped against each other rather than trusted: an author who types the smaller number into
	// the second field gets a passive that PUNISHES movement, which reads from inside the game as
	// the passive being broken rather than as a typo.
	const float Floor = FMath::Max(0.0f, Stats.DamageAtNoTravel);
	const float Ceiling = FMath::Max(Floor, Stats.DamageAtFullTravel);

	return FMath::Lerp(Floor, Ceiling, Fraction);
}

float UAbilityHandler_SniperPassive::GetTravelFraction() const
{
	const UAbilityDefinition_SniperPassive* Def = Cast<UAbilityDefinition_SniperPassive>(GetDefinition());
	if (!Def)
	{
		return 0.0f;
	}

	const FSniperPassiveLevelStats Stats = Def->GetStatsAtLevel(GetCurrentLevel());
	return FMath::Clamp(TravelSinceLastShot / FMath::Max(1.0f, Stats.DistanceForFullBonus), 0.0f, 1.0f);
}

float UAbilityHandler_SniperPassive::GetPendingPierceDamage() const
{
	return PierceDamageForTravel(TravelSinceLastShot);
}

float UAbilityHandler_SniperPassive::GetBonusPierceDamage(const AActor* Target) const
{
	// Target is ignored on purpose: what a shot is worth is a fact about the shooter, not about who
	// is standing in front of them. It stays in the signature because the hook is shared.
	return PierceDamageForTravel(TravelAtLastShot);
}

float UAbilityHandler_SniperPassive::GetPredictedPierceDamage(const AActor* Target) const
{
	return GetPendingPierceDamage();
}

void UAbilityHandler_SniperPassive::OnOwnerDealtDamage(AActor* Target, float Damage, bool bKilled)
{
	if (!Target || bKilled)
	{
		return;
	}

	UWorld* World = Target->GetWorld();
	if (!World)
	{
		return;
	}

	// Zero-damage hits count. A shot that only stripped shield is still the player having engaged
	// this enemy, and it is exactly the moment they most want to see what the next one would do.
	EngagedTargets.Add(Target, World->GetTimeSeconds());
	PruneEngagedTargets();
}

void UAbilityHandler_SniperPassive::PruneEngagedTargets()
{
	const UAbilityDefinition_SniperPassive* Def = Cast<UAbilityDefinition_SniperPassive>(GetDefinition());
	const float Memory = Def ? Def->ReadoutMemorySeconds : 0.0f;

	const UWorld* World = GetOwningCharacter() ? GetOwningCharacter()->GetWorld() : nullptr;
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	for (auto It = EngagedTargets.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
			continue;
		}

		if (Memory > 0.0f && Now - It.Value() > Memory)
		{
			It.RemoveCurrent();
		}
	}
}

bool UAbilityHandler_SniperPassive::GetPredictedShotDamage(const AActor* Target, float& OutDamage) const
{
	if (!Target)
	{
		return false;
	}

	// Only enemies this player has already shot. The readout is a lesson about the scaling, and it
	// can only teach it on a target the player has a previous number for; on everything else it
	// would be a screenful of figures nobody asked for.
	if (!EngagedTargets.Contains(const_cast<AActor*>(Target)))
	{
		return false;
	}

	const AShooterCharacter* Character = GetOwningCharacter();
	const AShooterWeapon* Weapon = Character ? Character->GetCurrentWeapon() : nullptr;
	if (!Weapon)
	{
		return false;
	}

	// The weapon answers the whole question, this passive's own damage included: it is the thing
	// that knows about heat, height, tags, upgrades and the shield gate, and the readout has to be
	// the TOTAL that would land or it is teaching the player a number they never see.
	// @see AShooterWeapon::PredictDamageAgainst
	OutDamage = Weapon->PredictDamageAgainst(const_cast<AActor*>(Target));
	return true;
}
