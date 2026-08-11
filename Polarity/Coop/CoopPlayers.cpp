// CoopPlayers.cpp

#include "Coop/CoopPlayers.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

void CoopPlayers::GetAll(const UWorld* World, TArray<APawn*>& OutPawns)
{
	OutPawns.Reset();

	if (!World)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}

		if (APawn* Pawn = PC->GetPawn())
		{
			OutPawns.Add(Pawn);
		}
	}
}

APawn* CoopPlayers::GetNearest(const UWorld* World, const FVector& Location)
{
	TArray<APawn*> Pawns;
	GetAll(World, Pawns);

	APawn* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();

	for (APawn* Pawn : Pawns)
	{
		const float DistSq = FVector::DistSquared(Location, Pawn->GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = Pawn;
		}
	}

	return Nearest;
}

bool CoopPlayers::IsPlayer(const AActor* Actor)
{
	const APawn* Pawn = Cast<APawn>(Actor);
	return Pawn && Pawn->IsPlayerControlled();
}

APlayerController* CoopPlayers::GetLocalController(const UWorld* World)
{
	if (!World || !GEngine)
	{
		return nullptr;
	}

	return GEngine->GetFirstLocalPlayerController(World);
}
