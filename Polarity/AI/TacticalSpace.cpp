// TacticalSpace.cpp

#include "AI/TacticalSpace.h"

#include "AI/PolarityTeams.h"
#include "AI/FactionContactMemory.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"

namespace
{
	/** Everybody currently on the board, cached for a fraction of a second.
	 *
	 *  The combat coordinator looked like the natural source and is not: NPCs register with it only
	 *  when they opt in and only once they start fighting, so on the squad stand its registry held a
	 *  single pawn and every position scored as empty ground - which is exactly why the first attempt
	 *  changed nothing on screen. A sweep is the honest answer, and the cache keeps it from being one
	 *  sweep per unit per pick: everybody scoring positions in the same instant shares one list.
	 */
	struct FCombatantCache
	{
		TWeakObjectPtr<UWorld> World;
		float BuiltAtTime = -1.0f;
		TArray<TWeakObjectPtr<APawn>> Pawns;
	};

	FCombatantCache GCombatantCache;
	constexpr float CombatantCacheSeconds = 0.2f;

	void GatherCombatants(const AActor* Asker, TArray<APawn*>& OutPawns)
	{
		OutPawns.Reset();

		UWorld* const World = Asker ? Asker->GetWorld() : nullptr;
		if (!World)
		{
			return;
		}

		const float Now = World->GetTimeSeconds();
		const bool bCacheGood = GCombatantCache.World.Get() == World
			&& Now - GCombatantCache.BuiltAtTime < CombatantCacheSeconds
			&& GCombatantCache.BuiltAtTime >= 0.0f;

		if (!bCacheGood)
		{
			GCombatantCache.World = World;
			GCombatantCache.BuiltAtTime = Now;
			GCombatantCache.Pawns.Reset();

			for (TActorIterator<APawn> It(World); It; ++It)
			{
				if (APawn* const Pawn = *It)
				{
					GCombatantCache.Pawns.Add(Pawn);
				}
			}
		}

		OutPawns.Reserve(GCombatantCache.Pawns.Num());
		for (const TWeakObjectPtr<APawn>& Entry : GCombatantCache.Pawns)
		{
			if (APawn* const Pawn = Entry.Get())
			{
				OutPawns.Add(Pawn);
			}
		}
	}
}

namespace TacticalSpace
{
	void BuildContext(const AActor* Asker, FSpaceContext& OutContext)
	{
		OutContext.Allies.Reset();
		OutContext.Enemies.Reset();

		if (!Asker)
		{
			return;
		}

		const uint8 OwnTeam = PolarityTeams::GetTeam(Asker);

		// Friends: where your own side actually is. A faction knows its own dispositions, so this one
		// is allowed to be exact.
		TArray<APawn*> Combatants;
		GatherCombatants(Asker, Combatants);

		for (APawn* const Pawn : Combatants)
		{
			if (!Pawn || Pawn == Asker || PolarityTeams::GetTeam(Pawn) != OwnTeam)
			{
				continue;
			}

			// Same side only. Neutrals (props, decoys, and players while the ignore switch is on) are
			// neither friends to gather around nor enemies to avoid.
			OutContext.Allies.Add(Pawn->GetActorLocation());
		}

		// Enemies: where your side has SEEN them. Reading live positions here would have every NPC
		// hold ground against opponents nobody has laid eyes on, and a line drawn against invisible
		// enemies is not tactics, it is the engine telling on itself. The faction's own memory of
		// sightings is the honest source, and it forgets on its own after twenty seconds.
		if (const UFactionContactMemory* const Memory = UFactionContactMemory::Get(Asker))
		{
			TArray<FFactionContact> Contacts;
			Memory->GetContacts(OwnTeam, Contacts);

			for (const FFactionContact& Contact : Contacts)
			{
				OutContext.Enemies.Add(Contact.LastKnownLocation);
			}
		}
	}

	float ScorePosition(const FSpaceContext& Context, const FVector& Candidate, const FSpaceWeights& Weights)
	{
		// Friendly influence: a bump centred on IdealAllySpacing rather than "closer is better".
		// Closer-is-better collapses a squad into one pile, which dies to one grenade and reads as a
		// bug; a bump keeps them in a loose cluster with room to shoot past each other.
		float AllyTerm = 0.0f;
		for (const FVector& Ally : Context.Allies)
		{
			const float Distance = FVector::Dist2D(Candidate, Ally);
			const float Offset = FMath::Abs(Distance - Weights.IdealAllySpacing);
			if (Offset < Weights.AllyTolerance)
			{
				AllyTerm += 1.0f - (Offset / Weights.AllyTolerance);
			}
		}

		// Two good neighbours is already "with the squad"; a tenth adds nothing, and without the cap
		// the biggest blob on the map would out-score every other consideration.
		AllyTerm = FMath::Min(AllyTerm, 2.0f) * 0.5f;

		// Hostile influence: linear ramp, worst on top of them, gone at EnemyFalloff. Summed, so
		// standing in the middle of four of them is four times as bad as passing one.
		float EnemyTerm = 0.0f;
		for (const FVector& Enemy : Context.Enemies)
		{
			const float Distance = FVector::Dist2D(Candidate, Enemy);
			if (Distance < Weights.EnemyFalloff)
			{
				EnemyTerm += 1.0f - (Distance / Weights.EnemyFalloff);
			}
		}

		EnemyTerm = FMath::Min(EnemyTerm, 2.0f) * 0.5f;

		return Weights.AllyWeight * AllyTerm - Weights.EnemyWeight * EnemyTerm;
	}
}
