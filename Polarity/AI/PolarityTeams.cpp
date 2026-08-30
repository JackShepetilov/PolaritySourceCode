// PolarityTeams.cpp

#include "AI/PolarityTeams.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GenericTeamAgentInterface.h"
#include "Coop/CoopPlayers.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarAIIgnorePlayers(
	TEXT("polarity.ai.IgnorePlayers"),
	0,
	TEXT("1 = no AI counts a player as an enemy: no acquisition, no coordinator group target, ")
	TEXT("standing player targets dropped. For watching a faction fight from inside it."),
	ECVF_Cheat);

namespace PolarityTeams
{
	bool ShouldIgnorePlayers()
	{
		return CVarAIIgnorePlayers.GetValueOnGameThread() != 0;
	}

	bool AreHostile(const AActor* A, const AActor* B)
	{
		if (!A || !B || A == B)
		{
			return false;
		}

		if (ShouldIgnorePlayers() && (CoopPlayers::IsPlayer(A) || CoopPlayers::IsPlayer(B)))
		{
			return false;
		}

		// A pawn answers for itself, because that is how the engine reads a stimulus source
		// (FGenericTeamId::GetTeamIdentifier casts the actor and stops there). The controller carries
		// the same number: AShooterAIController takes it from the pawn on possession.
		return FGenericTeamId::GetAttitude(A, B) == ETeamAttitude::Hostile;
	}

	uint8 GetTeam(const AActor* Actor)
	{
		return FGenericTeamId::GetTeamIdentifier(Actor).GetId();
	}

	void GatherHostilePawns(const AActor* Asker, TArray<APawn*>& OutPawns)
	{
		OutPawns.Reset();

		UWorld* const World = Asker ? Asker->GetWorld() : nullptr;
		if (!World)
		{
			return;
		}

		for (TActorIterator<APawn> It(World); It; ++It)
		{
			APawn* const Candidate = *It;
			if (AreHostile(Asker, Candidate))
			{
				OutPawns.Add(Candidate);
			}
		}
	}

	AActor* ResolveDamageSource(AActor* DamageCauser, AController* EventInstigator)
	{
		if (EventInstigator)
		{
			if (APawn* const InstigatorPawn = EventInstigator->GetPawn())
			{
				return InstigatorPawn;
			}
		}

		if (DamageCauser)
		{
			if (AActor* const Owner = DamageCauser->GetOwner())
			{
				return Owner;
			}
		}

		return DamageCauser;
	}

	APawn* FindNearestHostilePawn(const AActor* Asker)
	{
		if (!Asker)
		{
			return nullptr;
		}

		TArray<APawn*> Hostiles;
		GatherHostilePawns(Asker, Hostiles);

		const FVector From = Asker->GetActorLocation();
		APawn* Nearest = nullptr;
		float NearestDistSq = TNumericLimits<float>::Max();

		for (APawn* const Candidate : Hostiles)
		{
			const float DistSq = FVector::DistSquared(From, Candidate->GetActorLocation());
			if (DistSq < NearestDistSq)
			{
				NearestDistSq = DistSq;
				Nearest = Candidate;
			}
		}

		return Nearest;
	}
}
