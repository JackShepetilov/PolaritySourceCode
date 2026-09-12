// FactionContactMemory.cpp

#include "AI/FactionContactMemory.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"

UFactionContactMemory* UFactionContactMemory::Get(const UObject* WorldContext)
{
	const UWorld* const World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UFactionContactMemory>() : nullptr;
}

float UFactionContactMemory::Now() const
{
	const UWorld* const World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0f;
}

bool UFactionContactMemory::IsFresh(const FFactionContact& Contact, float CurrentTime) const
{
	// Somebody looking at it keeps it alive regardless of age: a contact under observation cannot be
	// stale by definition, and a long firefight would otherwise expire the target being shot at.
	return Contact.Enemy.IsValid()
		&& (Contact.bCurrentlySeen || CurrentTime - Contact.LastSeenTime <= ContactLifetime);
}

void UFactionContactMemory::ReportContact(uint8 ReporterTeam, AActor* Enemy, const FVector& Location, APawn* Reporter, bool bSeenNow)
{
	if (!Enemy)
	{
		return;
	}

	TArray<FFactionContact>& Contacts = ContactsByTeam.FindOrAdd(ReporterTeam);

	const float CurrentTime = Now();

	for (FFactionContact& Existing : Contacts)
	{
		if (Existing.Enemy.Get() != Enemy)
		{
			continue;
		}

		// A sighting always wins over a loss report, whoever it came from: two NPCs looking at the
		// same enemy and one of them turning away must not clear the side's knowledge.
		if (bSeenNow || !Existing.bCurrentlySeen)
		{
			Existing.LastKnownLocation = Location;
			Existing.LastSeenTime = CurrentTime;
			Existing.Reporter = Reporter;
		}

		if (bSeenNow)
		{
			Existing.bCurrentlySeen = true;
		}
		else if (Existing.Reporter.Get() == Reporter)
		{
			// Only the NPC that was holding the sighting can give it up.
			Existing.bCurrentlySeen = false;
		}

		return;
	}

	FFactionContact New;
	New.Enemy = Enemy;
	New.LastKnownLocation = Location;
	New.LastSeenTime = CurrentTime;
	New.bCurrentlySeen = bSeenNow;
	New.Reporter = Reporter;
	Contacts.Add(New);

	UE_LOG(LogTemp, Log, TEXT("[AI_DEBUG] team %d contact: %s at %s (by %s)"),
		static_cast<int32>(ReporterTeam), *GetNameSafe(Enemy), *Location.ToCompactString(), *GetNameSafe(Reporter));
}

void UFactionContactMemory::GetContacts(uint8 Team, TArray<FFactionContact>& OutContacts) const
{
	OutContacts.Reset();

	const TArray<FFactionContact>* const Contacts = ContactsByTeam.Find(Team);
	if (!Contacts)
	{
		return;
	}

	const float CurrentTime = Now();
	for (const FFactionContact& Contact : *Contacts)
	{
		if (IsFresh(Contact, CurrentTime))
		{
			OutContacts.Add(Contact);
		}
	}

	OutContacts.Sort([](const FFactionContact& A, const FFactionContact& B)
	{
		return A.LastSeenTime > B.LastSeenTime;
	});
}

bool UFactionContactMemory::GetContact(uint8 Team, const AActor* Enemy, FFactionContact& OutContact) const
{
	const TArray<FFactionContact>* const Contacts = ContactsByTeam.Find(Team);
	if (!Contacts || !Enemy)
	{
		return false;
	}

	const float CurrentTime = Now();
	for (const FFactionContact& Contact : *Contacts)
	{
		if (Contact.Enemy.Get() == Enemy && IsFresh(Contact, CurrentTime))
		{
			OutContact = Contact;
			return true;
		}
	}

	return false;
}

bool UFactionContactMemory::FindNearestContact(uint8 Team, const FVector& Location, FFactionContact& OutContact) const
{
	const TArray<FFactionContact>* const Contacts = ContactsByTeam.Find(Team);
	if (!Contacts)
	{
		return false;
	}

	const float CurrentTime = Now();
	float NearestDistSq = TNumericLimits<float>::Max();
	bool bFound = false;

	for (const FFactionContact& Contact : *Contacts)
	{
		if (!IsFresh(Contact, CurrentTime))
		{
			continue;
		}

		// Measured to the REMEMBERED position, not to the actor: an enemy that has walked away is
		// still worth searching for where it was, and using its real position here would quietly
		// turn a memory into omniscience.
		const float DistSq = FVector::DistSquared(Location, Contact.LastKnownLocation);
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			OutContact = Contact;
			bFound = true;
		}
	}

	return bFound;
}

void UFactionContactMemory::ForgetTeam(uint8 Team)
{
	ContactsByTeam.Remove(Team);
}
