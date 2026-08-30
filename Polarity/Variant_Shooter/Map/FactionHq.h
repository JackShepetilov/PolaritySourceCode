// FactionHq.h
// Where a faction's squads come from, and what the team can take away from it.
//
// A headquarters IS a point of interest, so this is an APoiActor: it has a place, a radius, a
// garrison standing in it and loot on its floor, and the team walks into it the same way it walks
// into anything else. Everything that is true of a point is inherited rather than written twice.
//
// What it adds is the two things only a headquarters does: it sends squads at points the faction
// does not hold, and it carries the objects that stop it doing so. The squads themselves are not its
// business - USquadSpawnSubsystem already knows how to put a loadout on the ground, give it a task
// and an objective, and let it break when it has had enough.
//
// What it takes away is capture. A headquarters is broken, not taken (seminar, section 10): four
// players besieging a base become a fourth faction and the run becomes an unbroken siege. That rule
// lives in the director, keyed on EPoiRole::Headquarters, so there is exactly one place where it can
// be true or false.
//
// Time spent here pays late: a headquarters with its reinforcements broken does not make the team
// stronger, it makes the enemy weaker at the final. That is one of the three corners of the triangle
// of time, and it is why this is worth building before the map is.

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Map/PoiActor.h"
#include "FactionHq.generated.h"

class USquadLoadout;
class ASabotageTarget;
class URunDirectorSubsystem;

/** One thing a headquarters can send out. */
USTRUCT(BlueprintType)
struct POLARITY_API FSortieEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sortie")
	TObjectPtr<USquadLoadout> Loadout = nullptr;

	/** This sortie is armour. Stops being sent once the hangar is broken. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sortie")
	bool bIsVehicle = false;

	/** Relative chance of being picked. Zero never gets sent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sortie", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;
};

UCLASS()
class POLARITY_API AFactionHq : public APoiActor
{
	GENERATED_BODY()

public:

	AFactionHq();

	// ==================== Identity ====================
	//
	// PoiTag, PoiRole, InfluenceRadius, the garrison and the loot are inherited. PoiRole is forced
	// to Headquarters in the constructor: a headquarters that says it is a mission point would be a
	// capturable base, which is the one thing this class exists to prevent.

	/** 1 = faction A, 2 = faction B. Matches APolarityCharacter::TeamByte, and StartingTeam is set
	 *  from it: a headquarters is held by its own side from the first frame and never changes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ", meta = (ClampMin = "1", ClampMax = "3"))
	uint8 FactionTeamId = 1;

	// ==================== Sorties ====================

	/** What this headquarters can send. Empty means a headquarters that only exists to be broken. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties")
	TArray<FSortieEntry> Sorties;

	/** Seconds before the first one leaves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties", meta = (ClampMin = "0.0"))
	float FirstSortieDelaySeconds = 60.0f;

	/** Seconds between sorties after that. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties", meta = (ClampMin = "10.0"))
	float SortieIntervalSeconds = 120.0f;

	/** Members are scattered this far around the gate when they spawn (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties", meta = (ClampMin = "100.0"))
	float SortieScatterRadius = 800.0f;

	/** Hard cap on sorties in one run. Zero is no cap. The population brake belongs to the faction
	 *  director when that exists; until then this stops a long run from filling the map. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties", meta = (ClampMin = "0"))
	int32 MaxSorties = 0;

	/** Where squads appear. Leave empty to use the actor's own location; point it at a gate mesh
	 *  otherwise, so squads do not walk out through a wall. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties")
	FName SortieSpawnPointTag = NAME_None;

	// ==================== Sabotage ====================

	/** The objects inside this headquarters that can be broken. Editor-placed and dragged in here:
	 *  an actor owning actors it can see, rather than spawning them at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sabotage")
	TArray<TObjectPtr<ASabotageTarget>> SabotageTargets;

	/** Called by a target when it goes. Tells the director, which is where the faction-wide effect
	 *  actually lives. */
	void NotifyTargetBroken(ASabotageTarget* Target);

	// ==================== Queries ====================

	UFUNCTION(BlueprintPure, Category = "HQ")
	int32 GetSortiesSent() const { return SortiesSent; }

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Pick a loadout, ask the director where the faction is needed, put the squad on the ground
	 *  with that point as its objective. Silently does nothing when the faction has no reinforcements
	 *  left, when nothing is worth marching at, or when the cap is reached. */
	void TrySendSortie();

	USquadLoadout* PickSortieLoadout(bool bAllowVehicles) const;

	float SortieTimer = 0.0f;
	int32 SortiesSent = 0;
};
