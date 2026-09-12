// NitroGateSubsystem.h
// Where the deployed nitro gates are, so the movement simulation can ask without a physics query.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NitroGateSubsystem.generated.h"

class ANitroGate;

/**
 * A list of the gates standing in this world, kept so UApexMovementComponent can answer "am I on
 * one" every simulated move for the price of a few distance checks.
 *
 * Per world rather than a static array, and that is not tidiness: PIE runs the listen server and the
 * client as two worlds in one process, so a static list would have each end launching people off the
 * other end's gates.
 *
 * Registration happens when a gate DEPLOYS, on every machine, not when it spawns - a gate still in
 * the air is not something to slide on.
 */
UCLASS()
class POLARITY_API UNitroGateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterGate(ANitroGate* Gate);
	void UnregisterGate(ANitroGate* Gate);

	/** The gate a pair of feet at this position is standing on, or null. First match wins: gates
	 *  are allowed to overlap and nobody can be launched by two of them in the same move anyway. */
	ANitroGate* FindGateUnder(const FVector& Feet) const;

private:
	/** Weak, and compacted lazily during the search: a gate can be shot out from under somebody in
	 *  the same frame they step on it, and EndPlay is not the only way one leaves the world. */
	mutable TArray<TWeakObjectPtr<ANitroGate>> Gates;
};
