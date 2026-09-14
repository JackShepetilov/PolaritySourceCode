// SiegeCoreBuildable.h
// The thing the siege is after when nobody is home.
//
// A building with a lot of health and one rule (the author's, 2026-09-14): while a player stands
// within DefendRadius of it, the enemy fights the players as usual; the moment nobody is that
// close, the enemy turns on the core itself. That is what makes leaving the base a decision: the
// turrets hold the line for a while, and the core is the clock that says how long.
//
// Placed straight into the level for now (no builder, no owner), so it starts Active at full
// health like any level-placed ABuildableActor. Without a UBuildableDefinition the wrench does
// nothing to it; repair needs one, see ReceiveWrenchHit.

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Buildables/BuildableActor.h"
#include "SiegeCoreBuildable.generated.h"

UCLASS(Blueprintable)
class POLARITY_API ASiegeCoreBuildable : public ABuildableActor
{
	GENERATED_BODY()

public:

	ASiegeCoreBuildable();

	/** No player within this of the core (cm): the enemy attacks the core instead of the players. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Siege", meta = (ClampMin = "0.0", Units = "cm"))
	float DefendRadius = 4000.0f;

	/** A player is within DefendRadius. Server-side answer: only the server sees every pawn. */
	UFUNCTION(BlueprintPure, Category = "Siege")
	bool IsDefended() const;

	/** The nearest standing core to From that no player is defending, or null when every core is
	 *  either defended or gone. What an attacker asks before it picks a pawn. */
	static ASiegeCoreBuildable* FindUndefended(const UWorld* World, const FVector& From);
};
