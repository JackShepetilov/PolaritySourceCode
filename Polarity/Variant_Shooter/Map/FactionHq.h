// FactionHq.h
// Where a faction's squads come from, and what the team can take away from it.
//
// A headquarters IS a point of interest, so this is an APoiActor: it has a place, a radius, a
// garrison standing in it and loot on its floor, and the team walks into it the same way it walks
// into anything else. Everything that is true of a point is inherited rather than written twice.
//
// What it adds is the one thing only a headquarters does: it sends squads at points the faction does
// not hold. The squads themselves are not its business - USquadSpawnSubsystem already knows how to
// put a loadout on the ground, give it a task and an objective, and let it break when it has had
// enough. What the team can take away is the banner standing in it (ABannerActor): break that and
// this headquarters spends the rest of the run sending its weakened list instead.
//
// What it takes away is capture. A headquarters is broken, not taken (seminar, section 10): four
// players besieging a base become a fourth faction and the run becomes an unbroken siege. That rule
// lives in the director, keyed on EPoiRole::Headquarters, so there is exactly one place where it can
// be true or false.
//
// Time spent here pays late: a broken headquarters does not make the team stronger, it makes the
// enemy weaker at the final. That is one of the three corners of the triangle of time, and it is why
// this is worth building before the map is.

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Map/PoiActor.h"
#include "FactionHq.generated.h"

class USquadLoadout;
class URunDirectorSubsystem;

/** One thing a headquarters can send out. */
USTRUCT(BlueprintType)
struct POLARITY_API FSortieEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sortie")
	TObjectPtr<USquadLoadout> Loadout = nullptr;

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

	/** What this headquarters sends while its banner still stands. Empty means a headquarters that
	 *  only exists to be broken. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties")
	TArray<FSortieEntry> Sorties;

	/** What it sends after the banner is broken, for the rest of the run.
	 *
	 *  This is the whole reward for taking a headquarters apart, and it is deliberately a different
	 *  LIST rather than a multiplier: the faction should not merely arrive in smaller numbers, it
	 *  should arrive without the thing that made it frightening. Leave it empty and a broken
	 *  headquarters stops sending anything at all, which is the bluntest version of the same idea. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties")
	TArray<FSortieEntry> WeakenedSorties;

	/** Seconds before the first one leaves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties", meta = (ClampMin = "0.0"))
	float FirstSortieDelaySeconds = 60.0f;

	/** Seconds between sorties after that. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties", meta = (ClampMin = "10.0"))
	float SortieIntervalSeconds = 120.0f;

	/** Members are scattered this far around the gate when they spawn (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties", meta = (ClampMin = "100.0"))
	float SortieScatterRadius = 800.0f;

	/** What this headquarters sends to somewhere it already holds and is losing.
	 *
	 *  A separate list because reinforcing is a different job: what you want arriving at a place
	 *  under attack is a garrison, not an assault. Empty falls back to Sorties, which is worse but
	 *  not broken. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties")
	TArray<FSortieEntry> ReinforcementSorties;

	// ==================== Eyes ====================

	/** Who this headquarters sends out alone to look: the scout drone and the sniper.
	 *
	 *  A separate list from every other sortie because these are not a squad. One entry sends one
	 *  man, and what leaves is marked a lone operator, which exempts it from the merge, the surplus
	 *  split and squad nerve - see USquad::bLoneOperator. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Scouts")
	TArray<FSortieEntry> ScoutSorties;

	/** How many of them this side keeps alive at once. Zero switches the whole thing off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Scouts", meta = (ClampMin = "0", ClampMax = "8"))
	int32 ScoutsWanted = 2;

	/** Seconds before a dead one is replaced. Long on purpose: killing the eyes should be worth
	 *  something, and a side that instantly re-grows them was never blinded at all. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Scouts", meta = (ClampMin = "5.0"))
	float ScoutRespawnSeconds = 90.0f;

	/** Hard cap on sorties in one run. Zero is no cap, and zero is now the default.
	 *
	 *  Was 3 on the bench, and that was the wrong brake: the whole war ended 2 minutes 20 seconds
	 *  into a run whose final does not open until minute fifteen. A count of sorties says nothing
	 *  about how crowded the map is, which is the thing anybody actually cared about. That is what
	 *  FactionPopulationCap does now; this stays for emergencies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties", meta = (ClampMin = "0"))
	int32 MaxSorties = 0;

	/** How many pawns of this faction may stand on the map at once, garrisons included.
	 *
	 *  The real brake, and the reason sorties can now be endless. Checked at the moment a squad
	 *  leaves: under the ceiling it goes, at the ceiling it waits, and in between it goes SHORT.
	 *  A cut-down squad rather than no squad on purpose - a ceiling that skips whole sorties makes
	 *  the war go quiet exactly when it is busiest, which looks like the system broke. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties", meta = (ClampMin = "1"))
	int32 FactionPopulationCap = 26;

	/** Fewer than this and it is not worth opening the gates. Two men walking at a garrison is not
	 *  a sortie, it is a delivery. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties", meta = (ClampMin = "1"))
	int32 MinSortieMembers = 2;

	/** Where squads appear. Leave empty to use the actor's own location; point it at a gate mesh
	 *  otherwise, so squads do not walk out through a wall. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HQ|Sorties")
	FName SortieSpawnPointTag = NAME_None;

	// ==================== Queries ====================

	UFUNCTION(BlueprintPure, Category = "HQ")
	int32 GetSortiesSent() const { return SortiesSent; }

	/** A headquarters answers a broken banner by dropping to its weakened list, not by spilling
	 *  loot, so it takes the parent's hook over. */
	virtual void NotifyBannerBroken(ABannerActor* BrokenBanner, AActor* Breaker) override;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Pick a loadout, ask the director where the faction is needed, put the squad on the ground with
	 *  that point as its objective. Silently does nothing when the list it is entitled to is empty,
	 *  when nothing is worth marching at, or when the cap is reached. */
	void TrySendSortie();

	/** Keep the eyes topped up. Runs on the same tick as the sorties, on its own clock. */
	void TickScouts(float DeltaSeconds);

	/** Who is out looking right now. Weak, so a dead scout simply drops off the list. */
	TArray<TWeakObjectPtr<APawn>> Scouts;

	float ScoutTimer = 0.0f;

	/** Draw from whichever list this headquarters is entitled to right now. Reinforcement draws
	 *  from its own list when there is one, because a place under attack wants a garrison. */
	USquadLoadout* PickSortieLoadout(EFactionAction Action) const;

	/** Draw from one table. */
	static USquadLoadout* PickWeighted(const TArray<FSortieEntry>& Table);

	/** How many members the loadout would put down at full strength. */
	static int32 LoadoutSize(const USquadLoadout* Loadout);

	float SortieTimer = 0.0f;
	int32 SortiesSent = 0;
};
