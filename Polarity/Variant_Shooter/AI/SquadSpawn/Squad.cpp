// Squad.cpp

#include "Variant_Shooter/AI/SquadSpawn/Squad.h"

#include "GameFramework/Pawn.h"

int32 USquad::AliveCount() const
{
	int32 Alive = 0;
	for (const TWeakObjectPtr<APawn>& Weak : Members)
	{
		const APawn* const Pawn = Weak.Get();
		if (Pawn && IsValid(Pawn) && !Pawn->IsPendingKillPending())
		{
			++Alive;
		}
	}
	return Alive;
}

bool USquad::GatherAlive(TArray<APawn*>& OutPawns, FVector& OutCentre) const
{
	OutPawns.Reset();
	OutCentre = FVector::ZeroVector;

	for (const TWeakObjectPtr<APawn>& Weak : Members)
	{
		APawn* const Pawn = Weak.Get();
		if (Pawn && IsValid(Pawn) && !Pawn->IsPendingKillPending())
		{
			OutPawns.Add(Pawn);
			OutCentre += Pawn->GetActorLocation();
		}
	}

	if (OutPawns.IsEmpty())
	{
		return false;
	}

	OutCentre /= static_cast<float>(OutPawns.Num());
	return true;
}

float USquad::Strength() const
{
	return static_cast<float>(AliveCount()) / static_cast<float>(FMath::Max(1, InitialCount));
}

bool USquad::CanSplit(float Now, float CooldownSeconds) const
{
	// A withdrawing squad is not available for reorganisation at any price. Splitting a force that
	// is running away produces two smaller forces running away, which is worse in every respect.
	if (bWithdrawing)
	{
		return false;
	}

	// Only its own last split. A merge does NOT hold this back: reinforcements arriving are the
	// reason a place has a surplus, so letting the merge block the split is how six men sit on a
	// point that wants three while a fight is lost within walking distance.
	return (Now - LastSplitTime) >= CooldownSeconds;
}

bool USquad::CanMerge(float Now, float CooldownSeconds) const
{
	// Merging into a withdrawal hands a healthy squad somebody else's panic.
	if (bWithdrawing)
	{
		return false;
	}

	// Both clocks here: a squad that has just detached must not swallow the detachment back before
	// it has walked out of the merge radius.
	return (Now - LastMergeTime) >= CooldownSeconds
		&& (Now - LastSplitTime) >= CooldownSeconds;
}

void USquad::SetTask(ESquadTask NewTask, FName NewObjectiveTag, const FVector& NewObjective,
	bool bHasNewObjective)
{
	Task = NewTask;
	ObjectiveTag = NewObjectiveTag;
	Objective = NewObjective;
	bHasObjective = bHasNewObjective;
}

void USquad::EnsureCommander()
{
	const APawn* const Current = Commander.Get();
	if (Current && IsValid(Current) && !Current->IsPendingKillPending())
	{
		return;
	}

	// The marked one is gone. Whoever is left takes over, and the mark is spent: a promotion is a
	// promotion, and a later spawn from the same loadout must not be able to displace the body that
	// has been leading this squad through a fight.
	Commander = nullptr;
	bCommanderWasChosen = false;

	for (const TWeakObjectPtr<APawn>& Weak : Members)
	{
		APawn* const Pawn = Weak.Get();
		if (Pawn && IsValid(Pawn) && !Pawn->IsPendingKillPending())
		{
			Commander = Pawn;
			return;
		}
	}
}

void USquad::AddMember(APawn* Pawn)
{
	if (!Pawn)
	{
		return;
	}

	Members.AddUnique(Pawn);
}

void USquad::RemoveMember(APawn* Pawn)
{
	if (!Pawn)
	{
		return;
	}

	Members.RemoveAll([Pawn](const TWeakObjectPtr<APawn>& Weak) { return Weak.Get() == Pawn; });

	if (Commander.Get() == Pawn)
	{
		EnsureCommander();
	}
}

void USquad::PruneDead()
{
	Members.RemoveAll([](const TWeakObjectPtr<APawn>& Weak)
	{
		const APawn* const Pawn = Weak.Get();
		return !Pawn || !IsValid(Pawn) || Pawn->IsPendingKillPending();
	});
}
