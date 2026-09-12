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

	/** Points do this once each per run; the flag lives here so a sublevel reload cannot refill a
	 *  point with a fresh garrison. */
	bool TryClaimGarrisonSpawn(FName PoiTag);

	/** The generic version of the same idea, for anything that must happen once per run and is not a
	 *  point: loot anchors roll through here. Returns true to the FIRST caller with a given key and
	 *  false to every caller after it, however many times the level streams in and out. */
	bool TryClaimOnce(FName Key);

	// ==================== Missions ====================

	/** The team did what the mission asked. Pays into the conditions of the final. Ignored when the
	 *  window has already shut: a mission whose point has fallen is over, not late. */
	UFUNCTION(BlueprintCallable, Category = "Run Director|Missions")
	bool CompleteMission(FName PoiTag);

	/** Everything the missions have paid so far. The final reads this when it builds itself. */
	UFUNCTION(BlueprintPure, Category = "Run Director|Missions")
	FFinalConditions GetEarnedFinalConditions() const { return EarnedConditions; }

	// ==================== Banners ====================

	/** The thing standing in the middle of a place has been broken. Recorded here rather than on the
	 *  point, so it survives the point being streamed out; what it costs is decided by the point. */
	void NotifyBannerBroken(FName PoiTag, AActor* Breaker);

	UFUNCTION(BlueprintPure, Category = "Run Director|Banners")
	bool IsBannerBroken(FName PoiTag) const;

	/** How many banners are still standing, for the overlay and for anything that wants to know how
	 *  much of the map has been taken apart. */
	UFUNCTION(BlueprintPure, Category = "Run Director|Banners")
	int32 GetBannersBrokenCount() const;

	// ==================== The war plan ====================
	//
	// Replaces GetSortieTarget, which picked the nearest point the faction did not hold and nothing
	// else. Measured on the bench 2026-09-04: from the two headquarters that rule sent faction A at
	// the neutral southern plain and faction B at the neutral final, twelve squads that walked to
	// opposite ends of the map and never met anybody. The bonus meant to pull them together only
	// applied to points that were ALREADY contested, so it could never start a war, only continue
	// one.
	//
	// A faction now wants the WHOLE map and ranks every place on it. Cheap ground first (nobody
	// holds it), enemy ground once the cheap ground is gone, and its own ground when it is being
	// taken away. That ordering falls out of the scoring; it is not a state machine.

	/** Every place this faction has an opinion about, best first. Empty when it holds everything.
	 *
	 *  Rebuilt on demand rather than stored: an order of conquest written at the start of a run is
	 *  wrong by the second engagement.
	 *
	 *  ForceAvailable is what the asker is prepared to commit right now. It belongs in the odds and
	 *  not in the caller's head: measured 2026-09-06, a plan that counted only the men ALREADY at a
	 *  place gave every attack a chance of exactly zero, because an attacker has nobody there yet.
	 *  Nothing ever cleared the threshold, and the only reason anybody marched at all was impatience
	 *  dragging the bar down to nothing - which looks from outside like a faction choosing at
	 *  random, and is the same thing. */
	void BuildFactionPlan(uint8 FactionTeamId, const FVector& From, int32 ForceAvailable,
		TArray<FFactionOrder>& OutPlan) const;

	/** The first line of that plan. False when the faction wants nothing, which is the honest
	 *  answer when it holds the map; marching somewhere for the look of it is how squads end up
	 *  walking in circles. */
	UFUNCTION(BlueprintCallable, Category = "Run Director|War")
	bool GetFactionOrder(uint8 FactionTeamId, const FVector& From, int32 ForceAvailable,
		FFactionOrder& OutOrder) const;

	/** The whole ranked plan into the log, one line per place. The answer to "why did they go
	 *  there", which is otherwise invisible from outside. Filter on [WAR_DEBUG]. */
	UFUNCTION(BlueprintCallable, Category = "Run Director|War")
	void DumpFactionPlan(uint8 FactionTeamId) const;

	/** A headquarters has put people on the road to somewhere. Counted as strength ALREADY THERE
	 *  for a while, which is what stops a faction feeding a fight one squad at a time: the second
	 *  squad sees the first one's odds, not the odds before it left. */
	void NotifySortieSent(uint8 FactionTeamId, FName PoiTag, int32 Members);

	/** How much of this faction is already committed to a place and has not arrived yet. */
	int32 GetCommittedForce(uint8 FactionTeamId, FName PoiTag) const;

	/** How many bodies this faction wants standing on a place.
	 *
	 *  The number that lets a squad SPLIT instead of moving as a lump. An order that says only
	 *  "attack" can only ever be obeyed by everybody; an order that says how many leaves a garrison
	 *  behind. Quiet ground wants a token watch, ground somebody is taking wants enough to hold it
	 *  against what we believe is there. */
	UFUNCTION(BlueprintPure, Category = "Run Director|War")
	int32 GetDemandAt(uint8 FactionTeamId, FName PoiTag) const;

	/** Somewhere worth watching, and somewhere to watch it from.
	 *
	 *  Picks the point this faction knows least about - staleness first, distance second - and
	 *  returns a spot on the FAR side of it, measured from this faction's own headquarters. That
	 *  offset is what "get into their rear" means in practice: approaching from behind keeps the
	 *  observer off the road its own side is using, which is exactly where the enemy is not looking.
	 *
	 *  False when there is nothing worth watching, which is a normal answer on a quiet map. */
	bool FindScoutPost(uint8 FactionTeamId, const FVector& From, FVector& OutLocation,
		FName& OutWatchTag) const;

	/** Сторона потеряла Count человек на точке PoiTag. Зовётся отрядной системой в тот момент,
	 *  когда она сама замечает убыль в отряде - другого места, где это видно, нет. */
	void NotifyLosses(uint8 FactionTeamId, FName PoiTag, int32 Count);

	/** Bodies a place gets just for being ours: enough to notice an attack and shoot back, not
	 *  enough to matter anywhere else. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "0"))
	int32 PlanTokenGarrison = 3;

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
	int32 GetMoneyStacksPlaced() const { return MoneyStacksOnMap; }

	/** A sheet has laid money out. Counted here because the audit is about the whole map, and money
	 *  now belongs to sheets rather than to points. */
	void ReportMoneyStacks(int32 Stacks);

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

	// ---- war plan weights ----
	//
	// All of these multiply a score that starts at 1. They are in one block on purpose: the balance
	// between them IS the faction's personality, and reading them apart from each other tells you
	// nothing.

	/** Distance at which a place is worth half as much. The logistics term: a faction that ignores
	 *  distance walks past a fight to reach a nicer one on the far side of the map. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "1000.0"))
	float PlanDistanceHalfLife = 22000.0f;

	/** Nobody holds it, so taking it costs a walk instead of a battle. Cheap ground first is what
	 *  makes a faction look like it has a plan rather than a grudge. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "1.0"))
	float PlanNeutralBonus = 1.7f;

	/** Somebody is already fighting there. Finishing a fight beats starting one, and this is what
	 *  turns two armies into one war instead of two parallel garrison swaps. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "1.0"))
	float PlanContestedBonus = 2.0f;

	/** Ground somebody else holds costs more than empty ground: there is a garrison, and taking it
	 *  is a fight rather than a walk. FLAT on purpose. The version this replaces scaled with how
	 *  deep into enemy country the place sat, which is a function of the distance to the asker, and
	 *  so charged the same distance twice - see the comment at the cost block in the .cpp. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "1.0"))
	float PlanHeldGroundCost = 1.25f;

	/** How much a place is worth for having been ignored, at full neglect. 1.5 means a corner of the
	 *  map this faction has not walked into for PlanStagnationSeconds is worth two and a half times
	 *  what it is worth the moment they leave it.
	 *
	 *  This is the only term in the plan that CHANGES WHEN NOTHING HAPPENS, and that is its whole
	 *  job. Every other number is a fact about the world, so a point that does not clear the bar
	 *  never clears it, and the map quietly shrinks to the few places that did. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "0.0"))
	float PlanStagnationBonus = 1.5f;

	/** How long being ignored takes to be worth the full bonus. Set to 0 to switch the term off.
	 *  Longer than PlanIntelSeconds on purpose: forgetting what is at a place and being drawn back
	 *  to it are different timescales, and tying them together would send everybody back the moment
	 *  their information expired. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "0.0"))
	float PlanStagnationSeconds = 120.0f;

	/** Missions and the final are worth more than plain loot, because losing them costs the faction
	 *  something the players want. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "1.0"))
	float PlanObjectiveBonus = 1.35f;

	/** Ours and being taken from us. Multiplied by how far the capture has got, so a place at 90%
	 *  screams and a place at 10% does not. Set below 1 for a faction that never looks back. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "0.0"))
	float PlanDefenceUrgency = 2.6f;

	// ---- is the game worth the candle ----
	//
	// The scoring above says how much a faction WANTS a place. On its own that is how an army walks
	// into a meat grinder: it wanted the place, it kept wanting it, and it fed itself in one squad
	// at a time. These four turn wanting into deciding.

	/** How long a departed squad still counts as strength at its destination. Long enough to cover
	 *  the walk; past that it either arrived and shows up in the live count, or it died on the way
	 *  and pretending otherwise is how a faction reinforces a place nobody reached. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "5.0"))
	float PlanCommitmentSeconds = 75.0f;

	/** How long a sighting is trusted. Older than this and the faction plans on a guess, which is
	 *  what makes its plans wrong in an interesting way rather than omniscient. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "5.0"))
	float PlanIntelSeconds = 45.0f;

	/** За сколько секунд память о бойне выцветает вдвое. Короткая: это не обида на всю жизнь, а
	 *  «там сейчас горячо». */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "5.0"))
	float PlanBloodHalfLife = 90.0f;

	/** Насколько один свежий труп удорожает поход в это место. Цена, а не приз: место не перестаёт
	 *  быть нужным оттого, что там убивают, оно перестаёт быть дешёвым. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "0.0"))
	float PlanBloodCost = 0.35f;

	/** Во сколько раз дорожает финальная точка, когда финал открылся.
	 *
	 *  Без этого открытие финала меняло правила только для игрока: фракции продолжали делить
	 *  окраины, и никакой финальной схватки не случалось вообще. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "1.0"))
	float PlanFinalBonus = 3.0f;

	/** How far from the watched point an observer sits. Far enough to be outside the fight, close
	 *  enough that eyes are worth having. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "1000.0"))
	float ScoutPostRadius = 12000.0f;

	/** How close a reported contact has to be to a point before it counts as being AT that point.
	 *
	 *  Only used for contacts with no destination of their own - a player, or a pawn whose squad
	 *  has been forgotten. Anybody in a squad is counted against the point that squad is marching
	 *  at, however far away he still is, which is the whole value of the warning. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "100.0"))
	float IntelContactRadius = 6000.0f;

	/** What a faction assumes is defending a place it has never looked at. Zero makes it charge
	 *  blindly into everything; too high and it never leaves home. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "0"))
	int32 PlanAssumedGarrison = 3;

	/** Odds it wants before committing. 1.0 is "even fight is fine"; 1.5 wants half again as many
	 *  as it thinks it is facing. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "0.1"))
	float PlanSuperiorityWanted = 1.4f;

	/** The candle. Below this the faction does NOT march, and waits instead. This one number is
	 *  the difference between an army and a mob. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "0.0"))
	float PlanWorthThreshold = 0.55f;

	/** Seconds of sitting still after which the threshold has fallen to zero.
	 *
	 *  Without this the two factions both decide nothing is worth it and the map goes silent, which
	 *  is the older complaint wearing a new hat. A side that has done nothing for a while starts
	 *  agreeing to worse odds, so somebody always eventually moves. */
	UPROPERTY(EditAnywhere, Config, Category = "Run Director|War", meta = (ClampMin = "1.0"))
	float PlanImpatienceSeconds = 100.0f;

private:
	/** Turn what the side's soldiers have seen into what its staff believes.
	 *
	 *  The observation channel already existed on both ends and was simply never joined: every
	 *  sighting goes into UFactionContactMemory, and nothing in this file ever read it. So a
	 *  rifleman could watch ten men walk past, tell his whole side, and the war plan would still
	 *  say the place was empty. This is that wire.
	 *
	 *  Runs at 1 Hz next to the other slow decisions; a second of lag on a number measured in tens
	 *  of seconds is not worth a faster loop. */
	void UpdateIntelFromContacts();

	/** Кровь стороны на точке, уже выцветшая на текущий момент. */
	float BloodAt(uint8 FactionTeamId, const FPoiWarState& State, float Now) const;


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

	/** Slow tick accumulator: the war is measured in minutes, not frames. */
	float TickAccumulator = 0.0f;

	/** Everything that has already happened once this run, by key. See TryClaimOnce. */
	TSet<FName> ClaimedOnce;

	/** Money stacks on the floor across the whole map, for the budget audit. */
	int32 MoneyStacksOnMap = 0;

	/** One squad on the road: who sent it, where, how many, when. */
	struct FFactionCommitment
	{
		uint8 TeamId = 255;
		FName PoiTag;
		int32 Members = 0;
		float SentAt = 0.0f;
	};
	TArray<FFactionCommitment> Commitments;

	/** World time each faction last decided something was worth doing. Feeds the impatience that
	 *  keeps the threshold from freezing the war. Indexed by team id, sized for players + A + B. */
	float LastCommitTime[4] = {0.0f, 0.0f, 0.0f, 0.0f};

	/** The threshold this faction is willing to accept right now, after impatience. */
	float CurrentWorthThreshold(uint8 FactionTeamId) const;

	/** Drop commitments that have run out their clock. */
	void PruneCommitments();
};
