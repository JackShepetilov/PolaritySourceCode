// FactionContactMemory.cpp

#include "AI/FactionContactMemory.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarAITargetKnowledge(
	TEXT("polarity.ai.targetknowledge"),
	1,
	TEXT("1: a side acts on what it knows of its enemies (sight in a cone, sound, the radio; the place he was last seen at once nobody sees him). 0: everybody knows where everybody is."));

bool UFactionContactMemory::IsKnowledgeOn()
{
	return CVarAITargetKnowledge.GetValueOnAnyThread() != 0;
}

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
	// By age alone. Whoever has it in view reports it again every fraction of a second, so a contact
	// under observation is never old; and "somebody is looking" as a flag outlived the one who set
	// it whenever that one died or was not the last to report.
	return Contact.Enemy.IsValid() && CurrentTime - Contact.LastSeenTime <= ContactLifetime;
}

bool UFactionContactMemory::IsSeenNow(const FFactionContact& Contact, float CurrentTime) const
{
	return Contact.bCurrentlySeen && CurrentTime - Contact.LastSeenTime <= SeenNowGrace;
}

bool UFactionContactMemory::GetKnownPlace(uint8 Team, const AActor* Enemy, FVector& OutPlace) const
{
	if (!Enemy)
	{
		return false;
	}
	if (!IsKnowledgeOn() || !Enemy->IsA<APawn>())
	{
		OutPlace = Enemy->GetActorLocation();
		return true;
	}

	const TArray<FFactionContact>* const Contacts = ContactsByTeam.Find(Team);
	if (!Contacts)
	{
		return false;
	}

	const float CurrentTime = Now();
	for (const FFactionContact& Contact : *Contacts)
	{
		if (Contact.Enemy.Get() == Enemy && IsFresh(Contact, CurrentTime))
		{
			OutPlace = IsSeenNow(Contact, CurrentTime) ? Enemy->GetActorLocation() : Contact.LastKnownLocation;
			return true;
		}
	}
	return false;
}

bool UFactionContactMemory::TryClaimSearch(uint8 Team, const AActor* Enemy, APawn* Who)
{
	if (!Enemy || !Who)
	{
		return false;
	}

	SearchClaims.RemoveAll([](const FSearchClaim& Claim) { return !Claim.Enemy.IsValid() || !Claim.Searcher.IsValid(); });

	for (const FSearchClaim& Claim : SearchClaims)
	{
		if (Claim.Team == Team && Claim.Enemy.Get() == Enemy)
		{
			return Claim.Searcher.Get() == Who;
		}
	}

	FSearchClaim& Claim = SearchClaims.AddDefaulted_GetRef();
	Claim.Team = Team;
	Claim.Enemy = Enemy;
	Claim.Searcher = Who;
	return true;
}

void UFactionContactMemory::ReleaseSearch(uint8 Team, const APawn* Who)
{
	SearchClaims.RemoveAll([Team, Who](const FSearchClaim& Claim)
	{
		return Claim.Team == Team && Claim.Searcher.Get() == Who;
	});
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
			FFactionContact& Out = OutContacts.Add_GetRef(Contact);
			Out.bCurrentlySeen = IsSeenNow(Contact, CurrentTime);
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
			OutContact.bCurrentlySeen = IsSeenNow(Contact, CurrentTime);
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
			OutContact.bCurrentlySeen = IsSeenNow(Contact, CurrentTime);
			bFound = true;
		}
	}

	return bFound;
}

void UFactionContactMemory::ForgetTeam(uint8 Team)
{
	ContactsByTeam.Remove(Team);
}
