// FactionContactMemory.h
// What a side knows about where its enemies are.
//
// Perception is private: an NPC that has never seen you does not know you exist, and that is what
// makes flanking work. But a fight between organised sides is not a set of private fights, and the
// F.E.A.R. lesson is that the thing worth sharing is not the decision, it is the OBSERVATION. One
// rifleman shouts a contact; the rest of the side now knows a position, and each of them still
// decides for itself what to do about it.
//
// So this holds one thing per side: where an enemy was last seen, when, and whether anybody still
// has eyes on it. Nothing here decides anything. Squads (the layer above) read it to route and to
// pick fights, and until squads exist it is already enough to stop the "enemy of another faction
// walks past ten NPCs and only the one who happened to look wins" problem.
//
// Deliberately keyed by TEAM and not by squad. A contact is knowledge, and knowledge does not stop
// at a squad boundary; who ACTS on it is a squad question, and lives with the squad.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FactionContactMemory.generated.h"

/** One remembered enemy, from one side's point of view. */
USTRUCT(BlueprintType)
struct FFactionContact
{
	GENERATED_BODY()

	/** Who was seen. Weak: a contact on a dead actor is simply forgotten, no cleanup pass needed. */
	UPROPERTY(BlueprintReadOnly, Category = "Contact")
	TWeakObjectPtr<AActor> Enemy;

	/** Where it was when it was last seen. NOT where it is: the whole point of a last-known position
	 *  is that it goes stale, and behaviour that searches it is searching a memory. */
	UPROPERTY(BlueprintReadOnly, Category = "Contact")
	FVector LastKnownLocation = FVector::ZeroVector;

	/** World seconds at the last sighting. */
	UPROPERTY(BlueprintReadOnly, Category = "Contact")
	float LastSeenTime = 0.0f;

	/** True while somebody on this side still has it in sight. The difference between "they are
	 *  there" and "they were there", which is the difference between shooting and searching. */
	UPROPERTY(BlueprintReadOnly, Category = "Contact")
	bool bCurrentlySeen = false;

	/** Who reported it last. For debugging, and later for "ask the one who saw it". */
	UPROPERTY(BlueprintReadOnly, Category = "Contact")
	TWeakObjectPtr<APawn> Reporter;

	bool IsValidContact() const { return Enemy.IsValid(); }
};

/**
 * Per-team memory of enemy positions. One instance per world.
 */
UCLASS()
class POLARITY_API UFactionContactMemory : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Convenience accessor. Null only outside a world. */
	static UFactionContactMemory* Get(const UObject* WorldContext);

	/** How long a sighting stays worth acting on (seconds). After this the contact is forgotten
	 *  entirely rather than kept as a weaker hint: a twenty second old position in a fight this fast
	 *  is not information, it is a wrong answer stated confidently. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction Memory", meta = (ClampMin = "1.0"))
	float ContactLifetime = 20.0f;

	/** Tell ReporterTeam that Enemy is at Location.
	 *
	 *  Called on every sighting, so it must stay cheap: one linear walk of a handful of entries.
	 *  Repeated reports of the same enemy refresh the one entry rather than adding another.
	 *
	 *  @param bSeenNow false means "this is where it went out of sight", which keeps the position
	 *         but drops the claim that anybody is looking at it. */
	UFUNCTION(BlueprintCallable, Category = "Faction Memory")
	void ReportContact(uint8 ReporterTeam, AActor* Enemy, const FVector& Location, APawn* Reporter, bool bSeenNow = true);

	/** Everything ReporterTeam currently believes, freshest first. Stale entries are dropped on the
	 *  way out, which is the only pruning this needs: nothing cares about a contact nobody asks for. */
	UFUNCTION(BlueprintCallable, Category = "Faction Memory")
	void GetContacts(uint8 Team, TArray<FFactionContact>& OutContacts) const;

	/** What this side knows about one specific enemy. False when it has never seen it, or has
	 *  forgotten. */
	UFUNCTION(BlueprintCallable, Category = "Faction Memory")
	bool GetContact(uint8 Team, const AActor* Enemy, FFactionContact& OutContact) const;

	/** Last known position of the enemy nearest to Location that this side remembers. Returns false
	 *  when the side knows of nobody, which is a normal answer and not an error. */
	UFUNCTION(BlueprintCallable, Category = "Faction Memory")
	bool FindNearestContact(uint8 Team, const FVector& Location, FFactionContact& OutContact) const;

	/** Forget everything one side knows. For arena resets and the test bench. */
	UFUNCTION(BlueprintCallable, Category = "Faction Memory")
	void ForgetTeam(uint8 Team);

private:
	/** Contacts per team. A handful of entries per side, walked linearly: a map keyed by actor would
	 *  cost more in bookkeeping than the walk saves at these sizes. */
	TMap<uint8, TArray<FFactionContact>> ContactsByTeam;

	/** Seconds of world time. Zero outside a world. */
	float Now() const;

	bool IsFresh(const FFactionContact& Contact, float CurrentTime) const;
};
