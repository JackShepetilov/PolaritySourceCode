// SiegeCampSubsystem.h
// Deals the mech parts out to the forest camps at the start of a run.
//
// Every camp registers here from its BeginPlay (server). One tick later, when all of them are in, the
// parts are dealt (Docs/Lane_Camps_Design_2026-09-30.md): each side of the forest (ASiegeCampSite::ForestSide,
// Top or Bot) gets all five slots, the rest of its camps a random slot each, so either side alone can
// build the mech. Rarity follows the camp's tier.
//
// Console (server): polarity.camp.list, polarity.camp.wake [slot name | all],
// polarity.camp.giveparts (every slot to each player, for testing the barn).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SiegeCampSubsystem.generated.h"

class ASiegeCampSite;

UCLASS()
class POLARITY_API USiegeCampSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Server: a camp is in the world. The deal happens once, a tick after the first one arrives. */
	void RegisterCamp(ASiegeCampSite* Camp);

	/** Every camp that registered and still exists. */
	void GetCamps(TArray<ASiegeCampSite*>& OutCamps) const;

	/** Deal the parts now. Re-deals if called again. */
	void AssignParts();

private:

	TArray<TWeakObjectPtr<ASiegeCampSite>> Camps;
	bool bDealScheduled = false;
};
