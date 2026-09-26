// GrappleFetchable.cpp

#include "GrappleFetchable.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

namespace GrappleFetch
{
	namespace
	{
		// One list for the process, filtered by world on the way out: a PIE session with several
		// players runs several worlds side by side, and each must only ever see its own items.
		TArray<TWeakObjectPtr<AActor>>& List()
		{
			static TArray<TWeakObjectPtr<AActor>> Fetchables;
			return Fetchables;
		}
	}

	void Register(AActor* Actor)
	{
		if (Actor)
		{
			List().AddUnique(Actor);
		}
	}

	void Unregister(AActor* Actor)
	{
		List().RemoveAll([Actor](const TWeakObjectPtr<AActor>& Entry) { return !Entry.IsValid() || Entry.Get() == Actor; });
	}

	IGrappleFetchable* Resolve(AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return nullptr;
		}
		for (UActorComponent* const Component : Actor->GetComponents())
		{
			if (IGrappleFetchable* const Fetchable = Cast<IGrappleFetchable>(Component))
			{
				return Fetchable;
			}
		}
		return Cast<IGrappleFetchable>(Actor);
	}

	const IGrappleFetchable* Resolve(const AActor* Actor)
	{
		return Resolve(const_cast<AActor*>(Actor));
	}

	void GetAll(const UWorld* World, TArray<AActor*>& OutActors)
	{
		OutActors.Reset();
		for (const TWeakObjectPtr<AActor>& Entry : List())
		{
			AActor* const Actor = Entry.Get();
			if (IsValid(Actor) && Actor->GetWorld() == World)
			{
				OutActors.Add(Actor);
			}
		}
	}
}
