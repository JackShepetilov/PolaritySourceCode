// ThreatComponent.cpp

#include "ThreatComponent.h"
#include "Engine/World.h"

UThreatComponent::UThreatComponent()
{
	// No tick on purpose. Threat is derived from the clock whenever somebody asks, so a quiet player
	// costs nothing at all and a loud one costs a short loop at 10Hz when the coordinator scores.
	PrimaryComponentTick.bCanEverTick = false;
}

void UThreatComponent::AddThreat(float Amount, float DecaySeconds)
{
	if (Amount <= 0.0f || DecaySeconds <= 0.0f)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	// Drop what has already faded before adding, so the list stays the length of "things that
	// happened recently" rather than growing for the whole fight.
	Impulses.RemoveAll([Now](const FThreatImpulse& Impulse)
	{
		return Now >= Impulse.StartTime + Impulse.Duration;
	});

	FThreatImpulse NewImpulse;
	NewImpulse.Amount = Amount;
	NewImpulse.StartTime = Now;
	NewImpulse.Duration = DecaySeconds;
	Impulses.Add(NewImpulse);
}

float UThreatComponent::GetThreat() const
{
	const UWorld* World = GetWorld();
	if (!World || Impulses.Num() == 0)
	{
		return 0.0f;
	}

	const float Now = World->GetTimeSeconds();

	float Total = 0.0f;
	for (const FThreatImpulse& Impulse : Impulses)
	{
		// Linear fade. Deliberately not a curve: the point is that it is over soon and that the
		// player can feel when, and a linear ramp is the easiest shape to feel.
		const float Elapsed = Now - Impulse.StartTime;
		const float Alpha = 1.0f - (Elapsed / Impulse.Duration);
		if (Alpha > 0.0f)
		{
			Total += Impulse.Amount * Alpha;
		}
	}

	return Total;
}

void UThreatComponent::ClearThreat()
{
	Impulses.Reset();
}
