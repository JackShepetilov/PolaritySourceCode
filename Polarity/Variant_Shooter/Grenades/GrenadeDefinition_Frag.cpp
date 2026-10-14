// GrenadeDefinition_Frag.cpp

#include "Variant_Shooter/Grenades/GrenadeDefinition_Frag.h"
#include "Variant_Shooter/Grenades/GrenadeProjectile.h"
#include "Variant_Shooter/DamageTypes/DamageType_Fragmentation.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Coop/CoopPlayers.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"

UGrenadeDefinition_Frag::UGrenadeDefinition_Frag()
{
	DamageType = UDamageType_Fragmentation::StaticClass();
	FuseTime = 3.0f;
	DangerRadius = 650.0f;
}

void UGrenadeDefinition_Frag::Detonate(AGrenadeProjectile* Grenade, const FVector& Origin) const
{
	UWorld* const World = Grenade ? Grenade->GetWorld() : nullptr;
	if (!World || !Grenade->HasAuthority())
	{
		return;
	}

	TArray<AActor*> Ignore;
	Ignore.Add(Grenade);
	if (!bHurtsPlayers)
	{
		TArray<APawn*> Players;
		CoopPlayers::GetAll(World, Players);
		Ignore.Append(Players);
	}

	// Who threw it: the kill is his. @see Source/AGENTS.md, attribution
	AController* const Thrower = Grenade->GetInstigatorController();

	// Who stood close enough to be put down, gathered BEFORE the damage: the ones it kills are taken
	// off below by asking whether they are still alive.
	TArray<AShooterNPC*> Near;
	if (KnockdownRadius > 0.0f)
	{
		FCollisionObjectQueryParams Objects(ECC_Pawn);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FragKnockdown), false, Grenade);
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, Objects, FCollisionShape::MakeSphere(KnockdownRadius), Params);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (AShooterNPC* const NPC = Cast<AShooterNPC>(Overlap.GetActor()))
			{
				Near.AddUnique(NPC);
			}
		}
	}

	// The engine's own: falloff, the wall check on Visibility, and a radial event that carries its
	// component hits (a hand-built one without them takes AShooterCharacter::TakeDamage down).
	UGameplayStatics::ApplyRadialDamageWithFalloff(World, BaseDamage, MinimumDamage, Origin, InnerRadius, OuterRadius,
		DamageFalloff, DamageType, Ignore, Grenade, Thrower, ECC_Visibility);

	for (AShooterNPC* const NPC : Near)
	{
		if (!IsValid(NPC) || NPC->IsDead())
		{
			continue;
		}

		// Behind a wall: the fragments did not reach him and neither does the blast.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FragKnockdownLine), false, Grenade);
		Params.AddIgnoredActor(NPC);
		FHitResult Blocked;
		if (World->LineTraceSingleByChannel(Blocked, Origin, NPC->GetActorLocation(), ECC_Visibility, Params))
		{
			continue;
		}

		APawn* ThrowerPawn = Grenade->GetInstigator();
		if (Thrower && Thrower->GetPawn())
		{
			ThrowerPawn = Thrower->GetPawn();
		}
		NPC->ApplyBlastKnockdown(Origin, ThrowerPawn);
	}
}
