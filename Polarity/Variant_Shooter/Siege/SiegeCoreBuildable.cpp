// SiegeCoreBuildable.cpp

#include "SiegeCoreBuildable.h"

#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

ASiegeCoreBuildable::ASiegeCoreBuildable()
{
	// A base, not a sentry: it has to survive a wave on its own.
	MaxHealth = 1000.0f;
}

bool ASiegeCoreBuildable::IsDefended() const
{
	TArray<APawn*> Players;
	CoopPlayers::GetAll(GetWorld(), Players);

	const float RadiusSq = FMath::Square(DefendRadius);
	const FVector Here = GetActorLocation();
	for (const APawn* const Player : Players)
	{
		if (Player && FVector::DistSquared(Here, Player->GetActorLocation()) <= RadiusSq)
		{
			return true;
		}
	}
	return false;
}

ASiegeCoreBuildable* ASiegeCoreBuildable::FindUndefended(const UWorld* World, const FVector& From)
{
	if (!World)
	{
		return nullptr;
	}

	// One or two cores per level: a plain walk is cheaper than any registry.
	ASiegeCoreBuildable* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<ASiegeCoreBuildable> It(World); It; ++It)
	{
		ASiegeCoreBuildable* const Core = *It;
		if (!IsValid(Core) || Core->IsDestroyed() || Core->IsDefended())
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(From, Core->GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = Core;
		}
	}
	return Nearest;
}
