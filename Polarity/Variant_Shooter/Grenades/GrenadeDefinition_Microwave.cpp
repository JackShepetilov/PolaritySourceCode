// GrenadeDefinition_Microwave.cpp

#include "Variant_Shooter/Grenades/GrenadeDefinition_Microwave.h"
#include "Variant_Shooter/Grenades/GrenadeProjectile.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "AI/Components/AIAccuracyComponent.h"
#include "EngineUtils.h"
#include "Coop/CoopPlayers.h"
#include "Variant_Shooter/Suppression/PlayerSuppressionComponent.h"
#include "Engine/World.h"

UGrenadeDefinition_Microwave::UGrenadeDefinition_Microwave()
{
	FuseTime = 2.0f;

	// Before it goes off they do not know what it is: a grenade lands by them and they get away from
	// it as from any. Once it works only the core drives them off. @see CoreRadius
	DangerRadius = 650.0f;
}

void UGrenadeDefinition_Microwave::Detonate(AGrenadeProjectile* Grenade, const FVector& Origin) const
{
	// The first pulse; the rest come from the grenade while it works. @see GetActiveSeconds
	const int32 Count = ApplyPulse(Grenade, Origin, /*bLog*/ true);
	UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] microwave at %s: %d soldiers within %.0f cm%s, works %.1f s"),
		*Origin.ToCompactString(), Count, MaxRadius, bStoppedByWalls ? TEXT(" (walls stop it)") : TEXT(""), ActiveSeconds);
}

void UGrenadeDefinition_Microwave::Pulse(AGrenadeProjectile* Grenade, const FVector& Origin) const
{
	ApplyPulse(Grenade, Origin, /*bLog*/ false);
}

int32 UGrenadeDefinition_Microwave::ApplyPulse(AGrenadeProjectile* Grenade, const FVector& Origin, bool bLog) const
{
	UWorld* const World = Grenade ? Grenade->GetWorld() : nullptr;
	if (!World || !Grenade->HasAuthority() || MaxRadius <= 0.0f)
	{
		return 0;
	}

	// Whole inside FullEffectRadius, then evenly down to the edge values; nothing past MaxRadius.
	auto Falloff = [this](float Distance, float& OutStrength, float& OutSeconds)
	{
		const float Edge = Distance <= FullEffectRadius ? 0.0f
			: FMath::Clamp((Distance - FullEffectRadius) / FMath::Max(1.0f, MaxRadius - FullEffectRadius), 0.0f, 1.0f);
		OutStrength = FMath::Lerp(Strength, EdgeStrength, Edge);
		OutSeconds = FMath::Lerp(Seconds, EdgeSeconds, Edge);
	};
	auto Blocked = [&](const AActor* Body)
	{
		if (!bStoppedByWalls)
		{
			return false;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MicrowaveLine), false, Grenade);
		Params.AddIgnoredActor(Body);
		FHitResult Hit;
		return World->LineTraceSingleByChannel(Hit, Origin, Body->GetActorLocation(), ECC_Visibility, Params);
	};

	// The players, the thrower too, as with the flash.
	TArray<APawn*> Players;
	CoopPlayers::GetAll(World, Players);
	for (APawn* const Player : Players)
	{
		UPlayerSuppressionComponent* const Suppression = Player ? Player->FindComponentByClass<UPlayerSuppressionComponent>() : nullptr;
		const float Distance = Player ? static_cast<float>(FVector::Dist(Origin, Player->GetActorLocation())) : MaxRadius + 1.0f;
		if (!Suppression || Distance > MaxRadius || Blocked(Player))
		{
			continue;
		}
		float HeadStrength = 0.0f, HeadSeconds = 0.0f;
		Falloff(Distance, HeadStrength, HeadSeconds);
		Suppression->ApplyHeadache(HeadStrength, HeadSeconds);
		if (bLog)
		{
			UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] microwave: player %s at %.0f cm, headache %.2f for %.1f s"),
				*Player->GetName(), Distance, HeadStrength, HeadSeconds);
		}
	}

	int32 Count = 0;
	for (TActorIterator<AShooterNPC> It(World); It; ++It)
	{
		AShooterNPC* const NPC = *It;
		UAIAccuracyComponent* const Accuracy = NPC ? NPC->GetAccuracyComponent() : nullptr;
		if (!Accuracy || NPC->IsDead())
		{
			continue;
		}

		const float Distance = static_cast<float>(FVector::Dist(Origin, NPC->GetActorLocation()));
		if (Distance > MaxRadius || Blocked(NPC))
		{
			continue;
		}
		float HeadStrength = 0.0f, HeadSeconds = 0.0f;
		Falloff(Distance, HeadStrength, HeadSeconds);

		Accuracy->ApplyHeadache(HeadStrength, HeadSeconds);
		++Count;

		if (bLog)
		{
			UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] microwave: %s at %.0f cm, headache %.2f for %.1f s"),
				*NPC->GetName(), Distance, HeadStrength, HeadSeconds);
		}
	}
	return Count;
}
