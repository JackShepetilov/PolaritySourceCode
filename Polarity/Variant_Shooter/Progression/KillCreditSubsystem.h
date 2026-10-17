// KillCreditSubsystem.h
// Who gets the experience for a kill: everyone who hurt that enemy [author, 2026-10-16].
//
// The enemies tell it from the one place every hit that writes their health goes through
// (AShooterNPC::ReportDamageNumber, shared by the infantry, the drones and the tank): the player
// behind the hit, then the death. A kill pays URunLevelDefinition::XPPerKill to each of them, in full,
// so in coop there is nothing to gain from stealing the last shot.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "KillCreditSubsystem.generated.h"

class APlayerState;

UCLASS()
class POLARITY_API UKillCreditSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	/** By hurt Victim. Server only. */
	static void NoteDamage(AActor* Victim, APlayerState* By);

	/** Victim died: pay everyone who hurt it, once. Server only. */
	static void NoteKill(AActor* Victim);

	/** Victim's kill has been paid already: a hit now lands on a body. */
	static bool IsPaid(const AActor* Victim);

private:

	TMap<TWeakObjectPtr<AActor>, TArray<TWeakObjectPtr<APlayerState>>> Contributors;

	/** Paid already: a hit on the body after the kill pays nothing. */
	TSet<TWeakObjectPtr<AActor>> Paid;

	/** Drop what belongs to actors that are gone. */
	void Prune();
};
