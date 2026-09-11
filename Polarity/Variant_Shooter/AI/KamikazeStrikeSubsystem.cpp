// KamikazeStrikeSubsystem.cpp

#include "KamikazeStrikeSubsystem.h"

#include "AI/PolarityTeams.h"
#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "KamikazeDroneNPC.h"
#include "ShooterCharacter.h"

static TAutoConsoleVariable<int32> CVarKamikazeMaxStrikes(
	TEXT("polarity.kamikaze.maxstrikes"),
	1,
	TEXT("How many kamikaze drones may be in flight at one player at the same time."));

static TAutoConsoleVariable<float> CVarKamikazeStrikeGap(
	TEXT("polarity.kamikaze.strikegap"),
	1.2f,
	TEXT("Seconds of rest a player gets after each kamikaze strike at them ends, before the next may start."));

namespace
{
	bool IsLiveDrone(const AKamikazeDroneNPC* Drone)
	{
		return IsValid(Drone) && !Drone->IsDead();
	}

	bool IsLivePlayer(const APawn* Pawn)
	{
		if (!IsValid(Pawn))
		{
			return false;
		}
		const AShooterCharacter* const Player = Cast<AShooterCharacter>(Pawn);
		return !Player || !Player->IsDead();
	}

	float BearingDeg(const FVector& From, const FVector& To)
	{
		const FVector D = To - From;
		return FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
	}
}

void UKamikazeStrikeSubsystem::Register(AKamikazeDroneNPC* Drone)
{
	if (IsValid(Drone))
	{
		Drones.AddUnique(Drone);
	}
}

void UKamikazeStrikeSubsystem::Unregister(AKamikazeDroneNPC* Drone)
{
	EndStrike(Drone);
	Drones.Remove(Drone);
	Assignment.Remove(Drone);
	for (TPair<TWeakObjectPtr<APawn>, FTargetState>& Pair : Targets)
	{
		Pair.Value.Waiting.Remove(Drone);
	}
}

void UKamikazeStrikeSubsystem::Cleanup()
{
	Drones.RemoveAll([](const TWeakObjectPtr<AKamikazeDroneNPC>& D) { return !IsLiveDrone(D.Get()); });

	for (auto It = Assignment.CreateIterator(); It; ++It)
	{
		if (!IsLiveDrone(It.Key().Get()) || !IsLivePlayer(It.Value().Get()))
		{
			It.RemoveCurrent();
		}
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	for (auto It = Targets.CreateIterator(); It; ++It)
	{
		if (!IsLivePlayer(It.Key().Get()))
		{
			It.RemoveCurrent();
			continue;
		}
		FTargetState& State = It.Value();
		State.Waiting.RemoveAll([](const TWeakObjectPtr<AKamikazeDroneNPC>& D) { return !IsLiveDrone(D.Get()); });
		// A strike whose drone vanished without ending it still ends here, and still earns the rest.
		const int32 Removed = State.Active.RemoveAll([](const TWeakObjectPtr<AKamikazeDroneNPC>& D) { return !IsLiveDrone(D.Get()); });
		if (Removed > 0)
		{
			State.LastStrikeEndTime = Now;
		}
		if (CountAssigned(It.Key().Get()) == 0 && State.Active.Num() == 0)
		{
			State.bHasBase = false;
		}
	}
}

int32 UKamikazeStrikeSubsystem::CountAssigned(const APawn* Target) const
{
	int32 Count = 0;
	for (const TPair<TWeakObjectPtr<AKamikazeDroneNPC>, TWeakObjectPtr<APawn>>& Pair : Assignment)
	{
		if (Pair.Value.Get() == Target && IsLiveDrone(Pair.Key.Get()))
		{
			++Count;
		}
	}
	return Count;
}

APawn* UKamikazeStrikeSubsystem::GetAssignedTarget(AKamikazeDroneNPC* Drone)
{
	UWorld* const World = GetWorld();
	if (!World || !IsLiveDrone(Drone))
	{
		return nullptr;
	}
	Cleanup();

	TArray<APawn*> Players;
	CoopPlayers::GetAll(World, Players);
	Players.RemoveAll([Drone](const APawn* P) { return !IsLivePlayer(P) || !PolarityTeams::AreHostile(Drone, P); });
	if (Players.Num() == 0)
	{
		Assignment.Remove(Drone);
		return nullptr;
	}

	int32 MinCount = TNumericLimits<int32>::Max();
	for (const APawn* P : Players)
	{
		MinCount = FMath::Min(MinCount, CountAssigned(P));
	}

	// Sticky: stay on the current player unless moving would actually even things out, which is
	// only when this one carries two or more above the lightest.
	if (APawn* const Current = Assignment.FindRef(Drone).Get())
	{
		if (Players.Contains(Current) && CountAssigned(Current) - MinCount < 2)
		{
			return Current;
		}
	}

	APawn* Best = nullptr;
	int32 BestCount = TNumericLimits<int32>::Max();
	float BestDistSq = TNumericLimits<float>::Max();
	const APawn* const Current = Assignment.FindRef(Drone).Get();
	for (APawn* P : Players)
	{
		// Leaving the current player takes this drone off their count.
		const int32 Count = CountAssigned(P) - (P == Current ? 1 : 0);
		const float DistSq = FVector::DistSquared(P->GetActorLocation(), Drone->GetActorLocation());
		if (Count < BestCount || (Count == BestCount && DistSq < BestDistSq))
		{
			Best = P;
			BestCount = Count;
			BestDistSq = DistSq;
		}
	}

	if (Best != Current)
	{
		EndStrike(Drone);
		for (TPair<TWeakObjectPtr<APawn>, FTargetState>& Pair : Targets)
		{
			Pair.Value.Waiting.Remove(Drone);
		}
		UE_LOG(LogTemp, Log, TEXT("[KAMIKAZE_QUEUE] %s -> %s"), *Drone->GetName(), *GetNameSafe(Best));
	}
	Assignment.Add(Drone, Best);
	return Best;
}

bool UKamikazeStrikeSubsystem::GetHoldSlot(const AKamikazeDroneNPC* Drone, const APawn* Target, int32& OutIndex, int32& OutCount, float& OutBaseBearingDeg)
{
	if (!Drone || !Target)
	{
		return false;
	}

	OutIndex = INDEX_NONE;
	OutCount = 0;
	for (const TWeakObjectPtr<AKamikazeDroneNPC>& D : Drones)
	{
		const AKamikazeDroneNPC* const Other = D.Get();
		if (IsLiveDrone(Other) && Assignment.FindRef(D).Get() == Target)
		{
			if (Other == Drone)
			{
				OutIndex = OutCount;
			}
			++OutCount;
		}
	}
	if (OutIndex == INDEX_NONE)
	{
		return false;
	}

	// The ring starts from wherever the first drone of this target came in, so a lone drone holds on
	// its own side and a ring does not swing round when it forms.
	FTargetState& State = Targets.FindOrAdd(const_cast<APawn*>(Target));
	if (!State.bHasBase)
	{
		State.BaseBearingDeg = BearingDeg(Target->GetActorLocation(), Drone->GetActorLocation());
		State.bHasBase = true;
	}
	OutBaseBearingDeg = State.BaseBearingDeg;
	return true;
}

bool UKamikazeStrikeSubsystem::RequestStrike(AKamikazeDroneNPC* Drone, APawn* Target, bool bPriority)
{
	UWorld* const World = GetWorld();
	if (!World || !IsLiveDrone(Drone) || !IsLivePlayer(Target))
	{
		return false;
	}
	Cleanup();

	FTargetState& State = Targets.FindOrAdd(Target);
	if (State.Active.Contains(Drone))
	{
		return true;
	}

	if (bPriority)
	{
		State.Waiting.Remove(Drone);
		State.Waiting.Insert(Drone, 0);
	}
	else
	{
		State.Waiting.AddUnique(Drone);
	}

	const int32 MaxStrikes = FMath::Max(1, CVarKamikazeMaxStrikes.GetValueOnGameThread());
	const bool bSlotFree = State.Active.Num() < MaxStrikes;
	const bool bRested = World->GetTimeSeconds() - State.LastStrikeEndTime >= CVarKamikazeStrikeGap.GetValueOnGameThread();
	const bool bFirstInLine = State.Waiting.Num() > 0 && State.Waiting[0] == Drone;
	if (!bSlotFree || !bRested || !bFirstInLine)
	{
		return false;
	}

	State.Waiting.RemoveAt(0);
	State.Active.Add(Drone);
	UE_LOG(LogTemp, Log, TEXT("[KAMIKAZE_QUEUE] %s strikes %s (%d waiting)"), *Drone->GetName(), *GetNameSafe(Target), State.Waiting.Num());
	return true;
}

void UKamikazeStrikeSubsystem::EndStrike(AKamikazeDroneNPC* Drone)
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	for (TPair<TWeakObjectPtr<APawn>, FTargetState>& Pair : Targets)
	{
		if (Pair.Value.Active.Remove(Drone) > 0)
		{
			Pair.Value.LastStrikeEndTime = Now;
		}
	}
}
