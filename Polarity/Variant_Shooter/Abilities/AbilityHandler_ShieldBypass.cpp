// AbilityHandler_ShieldBypass.cpp

#include "AbilityHandler_ShieldBypass.h"
#include "AbilityDefinition_ShieldBypass.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Weapons/ShieldBypassProjectile.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AShooterNPC* UAbilityHandler_ShieldBypass::FindTargetEnemy(float Range) const
{
	// The caster may have aimed this themselves while holding the key. Their pick wins: what the
	// brackets highlighted is what the bolt must leave for, or the reticle is a lie under any
	// latency at all. Falls back to scoring here when there was no held aim.
	if (OwningCharacter)
	{
		if (AShooterNPC* Aimed = OwningCharacter->GetAbilityAimTarget())
		{
			return Aimed;
		}
	}
	return ScoreBestTarget(OwningCharacter, Range);
}

AShooterNPC* UAbilityHandler_ShieldBypass::ScoreBestTarget(const AShooterCharacter* OwningCharacter, float Range)
{
	UWorld* World = OwningCharacter ? OwningCharacter->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	const FVector Origin = OwningCharacter->GetPawnViewLocation();
	const FVector Aim = OwningCharacter->GetBaseAimRotation().Vector();

	// Scored on being central AND being near, multiplied rather than added. Screen-centre alone
	// picks a distant enemy over the one in your face; distance alone picks whatever you happen to
	// stand beside regardless of where you are looking. Multiplying means neither term can carry a
	// candidate that is hopeless on the other.
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

		const float Centredness = FVector::DotProduct(Aim, ToEnemy);
		if (Centredness <= 0.0f)
		{
			continue;   // behind the player is never a candidate, however close
		}

		const float Score = Centredness * (1.0f - Distance / Range);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Enemy;
		}
	}

	return Best;
}

void UAbilityHandler_ShieldBypass::OnPerShotEffect_Implementation()
{
	const UAbilityDefinition_ShieldBypass* Def = Cast<UAbilityDefinition_ShieldBypass>(GetDefinition());
	if (!OwningCharacter || !Def)
	{
		return;
	}

	if (!Def->BypassProjectileClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldBypass: no BypassProjectileClass set on %s"),
			*GetNameSafe(Def));
		return;
	}

	// Nothing in sight is no longer a refusal. The bolt leaves anyway, flies where the player was
	// looking, and hunts for itself on the way -- which is what makes firing round a corner or over
	// cover work at all. Refusing here also punished the player for the ability's own aim rules.
	AShooterNPC* Target = FindTargetEnemy(Def->TargetSearchRange);

	// Same muzzle the burst uses, so the bolt leaves the hand the animation is throwing with rather
	// than from the camera.
	FVector SpawnLoc = OwningCharacter->GetPawnViewLocation();
	if (const USkeletalMeshComponent* FPMesh = OwningCharacter->GetFirstPersonMesh())
	{
		if (!Def->ProjectileSpawnSocket.IsNone())
		{
			SpawnLoc = FPMesh->GetSocketLocation(Def->ProjectileSpawnSocket);
		}
	}

	// With a target, aim at it; without one, straight down the player's aim and let the scan take over.
	const FRotator Facing = Target
		? (Target->GetActorLocation() - SpawnLoc).Rotation()
		: OwningCharacter->GetBaseAimRotation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwningCharacter;
	SpawnParams.Instigator = OwningCharacter;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AShieldBypassProjectile* Bolt = OwningCharacter->GetWorld()->SpawnActor<AShieldBypassProjectile>(
		Def->BypassProjectileClass, SpawnLoc, Facing, SpawnParams);
	if (!Bolt)
	{
		return;
	}

	Bolt->LaunchAt(Target, Def->ProjectileSpeed, Def->RedirectDamageMultiplier,
		Def->Duration, Def->MoveSpeedMultiplier);

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] ShieldBypass: bolt away at %s from socket %s"),
		Target ? *Target->GetName() : TEXT("nothing yet, will scan"),
		*Def->ProjectileSpawnSocket.ToString());
}
