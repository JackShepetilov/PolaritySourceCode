// KamikazeStrikeSubsystem.cpp

#include "KamikazeStrikeSubsystem.h"

#include "AI/PolarityTeams.h"
#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "KamikazeDroneNPC.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"

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

	float AngleBetweenDeg(float A, float B)
	{
		return FMath::Abs(FMath::FindDeltaAngleDegrees(A, B));
	}

	/** Hold points closer than this push apart (cm). */
	constexpr float SeparationRadius = 400.0f;
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

	for (auto It = Targets.CreateIterator(); It; ++It)
	{
		if (!IsLivePlayer(It.Key().Get()))
		{
			It.RemoveCurrent();
			continue;
		}
		It.Value().Waiting.RemoveAll([](const TWeakObjectPtr<AKamikazeDroneNPC>& D) { return !IsLiveDrone(D.Get()); });
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
	const APawn* const Current = Assignment.FindRef(Drone).Get();
	if (Current && Players.Contains(Current) && CountAssigned(Current) - MinCount < 2)
	{
		return const_cast<APawn*>(Current);
	}

	APawn* Best = nullptr;
	int32 BestCount = TNumericLimits<int32>::Max();
	float BestDistSq = TNumericLimits<float>::Max();
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
		for (TPair<TWeakObjectPtr<APawn>, FTargetState>& Pair : Targets)
		{
			Pair.Value.Waiting.Remove(Drone);
		}
		UE_LOG(LogTemp, Log, TEXT("[KAMIKAZE_QUEUE] %s -> %s"), *Drone->GetName(), *GetNameSafe(Best));
	}
	Assignment.Add(Drone, Best);
	return Best;
}

FVector UKamikazeStrikeSubsystem::GetSeparationOffset(const AKamikazeDroneNPC* Drone, const FVector& Point) const
{
	FVector Push = FVector::ZeroVector;
	for (const TWeakObjectPtr<AKamikazeDroneNPC>& D : Drones)
	{
		const AKamikazeDroneNPC* const Other = D.Get();
		if (!IsLiveDrone(Other) || Other == Drone)
		{
			continue;
		}
		// Measured against where the other drone actually is: two drones heading for nearby points
		// part as they arrive, instead of settling onto one spot.
		const FVector Away = Point - Other->GetActorLocation();
		const float Dist = Away.Size();
		if (Dist < SeparationRadius)
		{
			const FVector Dir = (Dist > 1.0f) ? Away / Dist : FVector(0.0f, 0.0f, 1.0f);
			Push += Dir * (SeparationRadius - Dist);
		}
	}
	return Push;
}

float UKamikazeStrikeSubsystem::PendingReloadAllowance(const APawn* Target, FTargetState& State, int32 KillBullets) const
{
	const AShooterCharacter* const Player = Cast<AShooterCharacter>(Target);
	const AShooterWeapon* const Weapon = Player ? Player->GetCurrentWeapon() : nullptr;
	if (!Weapon || !Weapon->UsesReload())
	{
		return 0.0f;
	}

	if (Weapon->GetBulletCount() >= FMath::Max(1, KillBullets))
	{
		// Enough in the magazine: the allowance is armed again for the next time it runs dry.
		State.bReloadAllowanceUsed = false;
		return 0.0f;
	}
	return State.bReloadAllowanceUsed ? 0.0f : Weapon->GetReloadTime();
}

bool UKamikazeStrikeSubsystem::RequestStrike(AKamikazeDroneNPC* Drone, APawn* Target, bool bPriority, float FlightTime)
{
	UWorld* const World = GetWorld();
	if (!World || !IsLiveDrone(Drone) || !IsLivePlayer(Target))
	{
		return false;
	}
	Cleanup();

	FTargetState& State = Targets.FindOrAdd(Target);
	if (bPriority)
	{
		State.Waiting.Remove(Drone);
		State.Waiting.Insert(Drone, 0);
	}
	else
	{
		State.Waiting.AddUnique(Drone);
	}
	if (State.Waiting[0] != Drone)
	{
		return false;
	}

	// The assumptions about the player come from the drone being scheduled: a drone type can be
	// kinder or harsher than another.
	const float Now = World->GetTimeSeconds();
	const float Kill = FMath::Max(0.0f, Drone->ScheduleKillTime);
	const float TurnSpeed = FMath::Max(1.0f, Drone->ScheduleTurnSpeed);
	const float Reaction = FMath::Max(0.0f, Drone->ScheduleReaction);
	const float Slack = FMath::Max(0.1f, Drone->ScheduleSlack);

	const float DroneBearing = BearingDeg(Target->GetActorLocation(), Drone->GetActorLocation());
	const float Impact = Now + FlightTime;

	// After the previous strike: the player kills that one, then turns from it to this one. Plus the
	// reload, once per empty magazine.
	float Earliest = -1.0f;
	float Reload = 0.0f;
	if (State.bHasLastStrike)
	{
		const float Turn = AngleBetweenDeg(State.LastStrikeBearingDeg, DroneBearing) / TurnSpeed;
		Reload = PendingReloadAllowance(Target, State, Drone->ScheduleKillBullets);
		Earliest = State.LastImpactTime + (Kill + Turn) * Slack + Reload;
	}
	if (Impact < Earliest)
	{
		return false;
	}

	// The first strike of a run has to be answerable from wherever the player is looking. Waiting
	// cannot fix a strike that is simply too short (the drone is holding too close), so this one is
	// reported rather than enforced: it means the hold distance is too small for the strike speed.
	const float ViewTurn = AngleBetweenDeg(Target->GetBaseAimRotation().Yaw, DroneBearing) / TurnSpeed;
	const float FirstNeeded = (Reaction + ViewTurn + Kill) * Slack;
	if (FlightTime < FirstNeeded && Now - State.LastImpactTime > 2.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[KAMIKAZE_QUEUE] %s: strike lands in %.2fs, a fair first strike needs %.2fs. Hold farther or strike slower."),
			*Drone->GetName(), FlightTime, FirstNeeded);
	}

	State.Waiting.RemoveAt(0);
	State.LastImpactTime = Impact;
	State.LastStrikeBearingDeg = DroneBearing;
	State.bHasLastStrike = true;
	if (Reload > 0.0f)
	{
		State.bReloadAllowanceUsed = true;
	}

	UE_LOG(LogTemp, Log, TEXT("[KAMIKAZE_QUEUE] %s strikes %s, lands in %.2fs%s (%d waiting)"),
		*Drone->GetName(), *GetNameSafe(Target), FlightTime, Reload > 0.0f ? TEXT(", reload allowance given") : TEXT(""),
		State.Waiting.Num());
	return true;
}
