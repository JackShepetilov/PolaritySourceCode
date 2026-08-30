// RunDirectorSubsystem.h
// The middle of the core loop: what is happening on the map, and why the team should go there now.
//
// The bottom of the game is the fight, the top is the meta, and this is the layer between them that
// the goals seminar (artifact "Kuda idyot igrok", 2026-08-27) said was missing. It owns the shape of
// a run: three mission points, two headquarters, plain loot between them, one final, and an
// extraction route that is only announced after the hold.
//
// It owns STATE, not actors. Points, headquarters and routes live in streamed sublevels and come and
// go; the war does not. Everything here is keyed by tag and survives a sublevel unloading, which is
// the whole reason this is a subsystem and not a manager actor sitting in one level.
//
// Authority: everything here runs on the server. TODO(COOP): clients currently learn none of this.
// Phase, mission windows and the announced route all need to reach the other three players before
// any of it can be shown on a HUD - most likely as replicated fields on a run state actor, since a
// world subsystem cannot replicate. Nothing else in this file assumes single player.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Variant_Shooter/Map/MapEventTypes.h"
#include "RunDirectorSubsystem.generated.h"

class APoiActor;
class AFactionHq;
class AExtractionPoint;
class AExtractionRoute;
class ARunLaunchPoint;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRunPhaseChanged, ERunPhase, NewPhase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPoiControlChanged, FName, PoiTag, uint8, NewTeam);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMissionWindowChanged, FName, PoiTag, bool, bOpen);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionCompleted, FName, PoiTag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnExtractionRouteAnnounced, AExtractionRoute*, Route);

UCLASS(Config = Game)
class POLARITY_API URunDirectorSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:

	// ==================== USubsystem ====================

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URunDirectorSubsystem, STATGROUP_Tickables);
	}

	/** The director for this world.
	 *
	 *  A world subsystem is unreachable from Blueprints and from editor Python without a static
	 *  getter, and both need it: a HUD has to read the phase and the mission windows, and the bench
	 *  has to assert on them. Everything else here is instance API; this is the door. */
	UFUNCTION(BlueprintPure, Category = "Run Director", meta = (WorldContext = "WorldContextObject"))
	static URunDirectorSubsystem* GetRunDirector(const UObject* WorldContextObject);

	// ==================== Registration ====================
	//
	// Everything in a sublevel registers on BeginPlay and unregisters on EndPlay. Unregistering a
	// point does NOT delete what the director knows about it: the state is frozen and picked up
	// again when the sublevel comes back.

	void RegisterPoi(APoiActor* Poi);
	void UnregisterPoi(APoiActor* Poi);

	void RegisterHq(AFactionHq* Hq);
	void UnregisterHq(AFactionHq* Hq);

	void RegisterExtractionRoute(AExtractionRoute* Route);
	void UnregisterExtractionRoute(AExtractionRoute* Route);

	// ==================== Points ====================

	/** Called by a loaded point on its own slow timer: who is standing on it right now. The director
	 *  turns this into capture progress, control flips and mission windows. */
	void ReportPoiPresence(const APoiActor* Poi, int32 PlayersPresent, int32 FactionAPresent,
		int32 FactionBPresent, float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "Run Director")
	bool GetPoiState(FName PoiTag, FPoiWarState& OutState) const;

	/** Read-only view of every point the run has seen, loaded or not. */
	const TArray<FPoiWarState>& GetAllPoiStates() const { return PoiStates; }

	/** The team that holds a point, or 255 when nobody does. */
	UFUNCTION(BlueprintPure, Category = "Run Director")
	uint8 GetPoiController(FName PoiTag) const;

	/** Points do this once each per run; the flags live here so a sublevel reload cannot refill a
	 *  point with loot or a fresh garrison. */
	bool TryClaimLootSpawn(FName PoiTag, int32 MoneyStacks);
	bool TryClaimGarrisonSpawn(FName PoiTag);

	// ==================== Missions ====================

	/** The team did what the mission asked. Pays into the conditions of the final. Ignored when the
	 *  window has already shut: a mission whose point has fallen is over, not late. */
	UFUNCTION(BlueprintCallable, Category = "Run Director|Missions")
	bool CompleteMission(FName PoiTag);

	/** Everything the missions have paid so far. The final reads this when it builds itself. */
	UFUNCTION(BlueprintPure, Category = "Run Director|Missions")
	FFinalConditions GetEarnedFinalConditions() const { return EarnedConditions; }

	// ==================== Headquarters ====================

	/** A headquarters lost one of its functions. Applies to the whole faction for the rest of the
	 *  run: headquarters are broken, not captured. */
	void NotifySabotage(uint8 FactionTeamId, ESabotageKind Kind);

	UFUNCTION(BlueprintPure, Category = "Run Director|Factions")
	bool CanFactionReinforce(uint8 FactionTeamId) const;

	UFUNCTION(BlueprintPure, Category = "Run Director|Factions")
	bool CanFactionFieldVehicles(uint8 FactionTeamId) const;

	UFUNCTION(BlueprintPure, Category = "Run Director|Factions")
	bool FactionHasPower(uint8 FactionTeamId) const;

	/** Where a headquarters should send its next sortie: the nearest point this faction does not
	 *  hold, preferring one that is already contested. Returns false when the faction holds
	 *  everything it can see, in which case nobody marches anywhere. */
	bool GetSortieTarget(uint8 FactionTeamId, const FVector& From, FVector& OutTarget, FName& OutPoiTag) const;

	// ==================== Final and extraction ====================

	UFUNCTION(BlueprintPure, Category = "Run Director")
	ERunPhase GetPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "Run Director")
	float GetRunSeconds() const { return RunSeconds; }

	/** How far the hold on the final has got, 0..1. Zero outside HoldingFinal. */
	UFUNCTION(BlueprintPure, Category = "Run Director")
	float GetHoldProgress() const;

	/** Picks one of the registered routes at random and tells everyone which. Called when the hold
	 *  finishes; exposed because the console command and the test bench need it too.
	 *
	 *  Random, and announced only now, on purpose: a route the team could have planned during the
	 *  hold is a route they walk, and the redistribution of the backpacks is supposed to happen
	 *  under fire, not before it. */
	UFUNCTION(BlueprintCallable, Category = "Run Director")
	AExtractionRoute* AnnounceExtractionRoute();

	/** The route the team was given, or null before it was announced. */
	UFUNCTION(BlueprintPure, Category = "Run Director")
	AExtractionRoute* GetAnnouncedRoute() const { return AnnouncedRoute.Get(); }

	/** Somebody boarded, or everybody died. Ends the run at this layer; URunSubsystem owns what
	 *  happens to the meta afterwards. */
	UFUNCTION(BlueprintCallable, Category = "Run Director")
	void EndRun(bool bExtracted);

	/** Where the team started the run. Read from ARunLaunchPoint, which already tags a level as a
	 *  run map and carries the sea toss. */
	UFUNCTION(BlueprintPure, Category = "Run Director")
	FVector GetPlayerInsertionLocation() const { return PlayerInsertion; }

	// ==================== Budget audit ====================

	/** Money stacks the whole map has put down so far, against the target. The dilemma the inventory
	 *  grid is built on only works inside a narrow band: a quarter to a third of the team's cells. */
	UFUNCTION(BlueprintPure, Category = "Run Director|Audit")
	int32 GetMoneyStacksPlaced() const;

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "Run Director|Events")
	FOnRunPhaseChanged OnPhaseChanged;

	UPROPERTY(BlueprintAssignable, Category = "Run Director|Events")
	FOnPoiControlChanged OnPoiControlChanged;

	UPROPERTY(BlueprintAssignable, Category = "Run Director|Events")
	FOnMissionWindowChanged OnMissionWindowChanged;

	UPROPERTY(BlueprintAssignable, Category = "Run Director|Events")
	FOnMissionCompleted OnMissionCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Run Director|Events")
	FOnExtractionRouteAnnounced OnExtractionRouteAnnounced;

	// ==================== Tuning ====================
	//
	// Defaults are the seminar's numbers. They are here rather than on an actor because they are
	// properties of a RUN, and a run spans the whole map; a designer who wants a shorter one edits
	// the ini, not thirty points.

	/** When the final opens, seconds into the run. Soft: it is a clock, not a gate, and a run where
	 *  everything went wrong still gets its ending. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|Tuning", meta = (ClampMin = "60.0"))
	float FinalOpensAfterSeconds = 900.0f;

	/** How long the team has to stand on the final before the route comes out (s). */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|Tuning", meta = (ClampMin = "10.0"))
	float FinalHoldSeconds = 120.0f;

	/** Seconds one side needs, unopposed, to take a point it does not hold. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|Tuning", meta = (ClampMin = "5.0"))
	float CaptureSeconds = 60.0f;

	/** Money stacks the map should carry in total: a quarter to a third of the team's cells, which
	 *  is 4-6 at sixteen cells. Only ever reported, never enforced. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|Tuning", meta = (ClampMin = "0"))
	int32 TargetMoneyStacks = 5;

private:

	FPoiWarState* FindState(FName PoiTag);
	const FPoiWarState* FindState(FName PoiTag) const;

	/** Creates the state on first sight of a point, so registration order does not matter. */
	FPoiWarState& FindOrAddState(const APoiActor* Poi);

	void SetPhase(ERunPhase NewPhase);

	/** Hand a point to a side: clears the capture, expires a mission the war has just closed, fires
	 *  the event. One place, so the console shortcut and a real capture cannot diverge. */
	void SetPoiController(FPoiWarState& State, uint8 NewTeam);

	/** Opens or shuts the window on a mission point and fires the event once per change. */
	void UpdateMissionWindow(FPoiWarState& State);

	/** Drives capture, hold and the announcement on the final point. */
	void TickFinal(const FPoiWarState& FinalState, int32 PlayersPresent, float DeltaSeconds);

	/** One line per point: who holds it, who is pushing, what the mission is doing. The answer to
	 *  "why is nothing happening", which is invisible from outside. */
	void DumpState() const;

	friend struct FRunDirectorConsole;

	// --- registries of what is currently loaded ---
	TArray<TWeakObjectPtr<APoiActor>> LoadedPois;
	TArray<TWeakObjectPtr<AFactionHq>> Headquarters;
	TArray<TWeakObjectPtr<AExtractionRoute>> Routes;

	// --- the run ---
	TArray<FPoiWarState> PoiStates;
	FFinalConditions EarnedConditions;
	ERunPhase Phase = ERunPhase::NotStarted;
	float RunSeconds = 0.0f;

	/** Seconds the team has been standing on the final. */
	float HoldSeconds = 0.0f;

	TWeakObjectPtr<AExtractionRoute> AnnouncedRoute;

	FVector PlayerInsertion = FVector::ZeroVector;

	/** Which faction lost which function. Indexed by team byte, so faction A is 1 and B is 2. */
	TSet<uint8> NoReinforcements;
	TSet<uint8> NoVehicles;
	TSet<uint8> NoPower;

	/** Slow tick accumulator: the war is measured in minutes, not frames. */
	float TickAccumulator = 0.0f;
};
