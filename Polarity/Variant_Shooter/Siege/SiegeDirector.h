// SiegeDirector.h
// The clock of the base defense.
//
// Waves come on a timer, never on a body count: the next one is due WaveInterval seconds after
// the last whether or not the last is dead. A player who leaves the base is not waited for, the
// enemy piles up at home and that pile is what calls them back. Nothing here resets on a death,
// nothing walls the arena off, nothing saves a checkpoint: that was AArenaManager's job for the
// old roguelike arenas, and the reasons it did not fit a siege are in
// Docs/Siege_Director_Plan_2026-09-14.md.
//
// Two phases. The AUTHORED waves are a hand-written list, one FArenaWave each, the same struct
// the arenas use. When they run out the ENDLESS phase begins: every wave gets a budget that grows
// by EndlessBudgetGrowth per wave, and spends it on kinds from EndlessPool by cost, weight and the
// wave each kind unlocks at. Growth above 1 is deliberate: a wave that only grows linearly is
// outrun by the metal it drops, and an endless mode has to end.
//
// The clock is a ceiling, not the only trigger: when the base goes quiet the next wave is sent
// early (bEarlyWaveOnLull), so the break a player gets is the walk in from the fog, not a timer.
// A wave comes out one enemy at a time (SpawnTrickleInterval), on a ring around the director
// rather than on placed points, and the carriers are capped: one rolled past MaxCarriersAlive
// becomes power for the living ones, and one clock here paces every carrier's drops.
//
// Server only: it spawns and it counts. The few numbers a HUD wants are replicated.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Arena/ArenaWaveData.h"
#include "Curves/CurveFloat.h"
#include "SiegeDirector.generated.h"

class AArenaSpawnPoint;
class ABuildableActor;
class AKamikazeCarrierDrone;
class AShooterNPC;
class ASiegeCoreBuildable;
class ASiegeLane;
class UPrimitiveComponent;

/** One kind of enemy the endless phase may buy. */
USTRUCT(BlueprintType)
struct FSiegeEnemyType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege")
	TSubclassOf<AShooterNPC> NPCClass;

	/** What one of these costs out of the wave budget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege", meta = (ClampMin = "0.01"))
	float Cost = 1.0f;

	/** First wave this kind may appear in, counting every wave of the siege from 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege", meta = (ClampMin = "1"))
	int32 FirstWave = 1;

	/** Relative pick weight among the kinds already unlocked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege", meta = (ClampMin = "0.01"))
	float Weight = 1.0f;
};

/** Where one pack stands in the schedule: what FSiegeCreepKind::CountFor needs to size a row. */
struct FSiegePackSlot
{
	/** Pass round the lane cycle, from 1. */
	int32 Wave = 1;

	/** Siege clock when the pack leaves (min): the counts grow on it. */
	float Minutes = 0.0f;

	/** Siege clock when the pack reaches the base (min): timed kinds are placed on it, so the player
	 *  meets them evenly spaced whatever the lane lengths. */
	float ArriveMinutes = 0.0f;

	/** Minutes between two packs on this same lane. */
	float LanePeriodMinutes = 1.5f;

	/** This lane's place in the cycle and how many lanes it has. */
	int32 LaneIndex = 0;
	int32 NumLanes = 1;

	int32 NumPlayers = 1;
	FName LaneName = NAME_None;
};

/**
 * One kind of creep in a lane pack (Docs/MOBA_Lanes_Concept_2026-09-29.md).
 *
 * Author 2026-09-30: every pack starts as 3 grunts and 1 carrier, and both grow with the clock, linearly
 * and for ever, no spikes. The tankette is on a timer of its own: once per lane every 9 min, the lanes
 * staggered, so the player meets one every 3 min.
 */
USTRUCT(BlueprintType)
struct FSiegeCreepKind
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege")
	TSubclassOf<AShooterNPC> NPCClass;

	/** How many per pack when CountByMinute has no keys. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege", meta = (ClampMin = "0"))
	int32 BaseCount = 1;

	/** X: minutes since the siege started. Y: how many per pack (rounded). Step keys make Dota's
	 *  "+1 at 15 min". No keys = BaseCount the whole run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege")
	FRuntimeFloatCurve CountByMinute;

	/** Added per player in the game, on top of the count. The opening pack of the author's plan is
	 *  BaseCount 0, CountPerPlayer 1, LastWave 1: one grunt per player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege", meta = (ClampMin = "0.0"))
	float CountPerPlayer = 0.0f;

	/** One more per pack every this many minutes of the siege, for ever: the linear growth. 0 = the
	 *  count does not grow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege", meta = (ClampMin = "0.0", Units = "min"))
	float AddOneEveryMinutes = 0.0f;

	/** On a timer instead of the wave rules: each lane gets this kind once every this many minutes,
	 *  the lanes spread evenly over the period in the cycle's order (9 min and three lanes = one every
	 *  3 min for the player). The pack that reaches the base first after the lane's time carries it.
	 *  0 = the wave rules below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege", meta = (ClampMin = "0.0", Units = "min"))
	float LaneCycleMinutes = 0.0f;

	/** Siege clock of the first one, on the first lane of the cycle (min). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege", meta = (ClampMin = "0.0", Units = "min", EditCondition = "LaneCycleMinutes > 0"))
	float LaneCycleFirstMinute = 3.0f;

	/** First wave this kind comes in, from 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege", meta = (ClampMin = "1"))
	int32 FirstWave = 1;

	/** From FirstWave on, only every Nth wave carries it (Dota's siege creep: FirstWave 11, 10). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege", meta = (ClampMin = "1"))
	int32 EveryNthWave = 1;

	/** Last wave it comes in. 0 = to the end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege", meta = (ClampMin = "0"))
	int32 LastWave = 0;

	/** Only these lanes (ASiegeLane::LaneName) get it. Empty = every open lane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege")
	TArray<FName> OnlyLanes;

	/** Count this kind comes to in this pack, 0 = not in it. */
	int32 CountFor(const FSiegePackSlot& Slot) const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSiegeStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSiegeWaveStarted, int32, WaveNumber);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSiegeBaseLost);

UCLASS(Blueprintable)
class POLARITY_API ASiegeDirector : public AActor
{
	GENERATED_BODY()

public:

	ASiegeDirector();

	// ==================== Authored waves ====================

	/** Hand-written waves, in order. Wave N of the siege is entry N-1 here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Waves")
	TArray<FArenaWave> AuthoredWaves;

	// ==================== Lanes ====================

	/** A lane map: every wave is one pack on each open ASiegeLane of the level, made of LanePack.
	 *  Replaces the authored and endless waves and the spawn ring while on. Off, or no lane on the
	 *  level, or an empty pack = the old ring siege (L_HomeBase). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes")
	bool bUseLanes = true;

	/** What one pack is made of, row by row. See FSiegeCreepKind for Dota's numbers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes", meta = (TitleProperty = "NPCClass"))
	TArray<FSiegeCreepKind> LanePack;

	/** One lane at a time, round the cycle, timed by ARRIVAL (author 2026-09-29 and 30): the packs
	 *  reach the base LanePackInterval apart, Mid, Top, Bot, Mid... and each leaves its lane's start
	 *  its walk earlier, so a long lane's pack leaves sooner than a short one's and two never land
	 *  together. A solo player walks the front from lane to lane. A wave is one pass round the cycle,
	 *  so FSiegeCreepKind's wave rules (FirstWave, EveryNthWave) count per lane; a pack with nobody in
	 *  it (the opening's Top and Bot) takes no slot. Off = every lane at once every WaveInterval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes")
	bool bStaggerLanes = true;

	/** Seconds between two packs reaching the base, for one player. Each lane then gets a pack every
	 *  LanePackInterval x (open lanes) seconds: 30 s and three lanes is a pack per lane every 90 s solo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes", meta = (ClampMin = "1.0", Units = "s", EditCondition = "bStaggerLanes"))
	float LanePackInterval = 30.0f;

	/** The pack rate grows with the players: the interval is divided by their number. Three players at
	 *  30 s get a pack every 10 s, a pack per lane every 30 s: Dota's rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes", meta = (EditCondition = "bStaggerLanes"))
	bool bPackRatePerPlayer = true;

	/** The order of the cycle, by ASiegeLane::LaneName. Lanes not listed come after, by name. Empty =
	 *  by name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes", meta = (EditCondition = "bStaggerLanes"))
	TArray<FName> LaneOrder;

	/** Seconds between two packs now (LanePackInterval over the players when bPackRatePerPlayer). */
	UFUNCTION(BlueprintPure, Category = "Siege")
	float GetLanePackInterval() const { return GetLanePackIntervalFor(GetHumanPlayerCount()); }

	/** The same for a given number of players. */
	float GetLanePackIntervalFor(int32 NumPlayers) const;

	/** People in the game, bots' player states left out (at least 1). */
	UFUNCTION(BlueprintPure, Category = "Siege")
	int32 GetHumanPlayerCount() const;

	/** The open lanes in the order of the cycle. */
	void GetCycleLanes(TArray<ASiegeLane*>& OutLanes) const;

	/** The siege has been going on before the player came (author 2026-09-30): the first pack reaches
	 *  the base PrewarmFirstArrival seconds after the dispenser goes down, the next LanePackInterval
	 *  after it, and so on. Every pack that by that schedule had to leave before the start appears at
	 *  the start where it would have walked to, as the real wave it is. Nothing exists before the
	 *  start, so nobody can be killed in advance. The pack counts and the upgrades read a clock that
	 *  starts when the first pack left. Off = the first pack leaves at the start from its lane's start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes", meta = (EditCondition = "bStaggerLanes"))
	bool bPrewarm = true;

	/** Seconds from the dispenser to the first pack at the base, when bPrewarm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes", meta = (ClampMin = "0.0", Units = "s", EditCondition = "bPrewarm"))
	float PrewarmFirstArrival = 10.0f;

	/** Where the arrival schedule stands: the wave and the lane of the cycle it looks at next, and when
	 *  the next pack with anybody in it reaches the base. */
	struct FLanePlanCursor
	{
		int32 Wave = 1;
		int32 LaneIndex = 0;
		float NextArrival = 0.0f;
	};

	/** One pack of the arrival schedule. Counts is per LanePack row. */
	struct FPlannedPack
	{
		int32 Wave = 0;
		ASiegeLane* Lane = nullptr;
		float LeaveAt = 0.0f;
		float ArriveAt = 0.0f;
		int32 Total = 0;
		TArray<int32> Counts;
	};

	/** The next pack with anybody in it, from Cursor on, and the cursor moved past it. Empty packs are
	 *  skipped without taking a slot. The composition reads the clock at the pack's leave time, from
	 *  ClockStart. False if a long run of lanes gave nothing (an empty LanePack). */
	bool PlanNextPack(FLanePlanCursor& Cursor, const TArray<ASiegeLane*>& Lanes, int32 NumPlayers, float ClockStart, FPlannedPack& Out) const;

	/** Seconds a walker takes from this lane's start to its base end, at CreepLaneSpeed. */
	float GetLaneWalkSeconds(const ASiegeLane* Lane) const;

	/** How fast a walker covers its lane, for the arithmetic of arrivals (cm/s). 0 = the slowest walker
	 *  of the pack's MaxWalkSpeed. Measured 2026-09-29 on L_Lanes: about 420 along the mid lane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes", meta = (ClampMin = "0.0", Units = "cm/s"))
	float CreepLaneSpeed = 420.0f;

	/** Creep health grows by (CreepHealthPerUpgrade - 1) of the base every this many seconds of the siege,
	 *  smoothly and linearly, for ever (author 2026-09-30: no spikes, no exponent). 0 = never. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes", meta = (ClampMin = "0.0", Units = "s"))
	float CreepUpgradeInterval = 300.0f;

	/** Health multiplier reached after one CreepUpgradeInterval: 1.10 is +10% of the base per interval,
	 *  x1.9 at 45 min with 300 s. Added, not compounded. 1 = no growth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes", meta = (ClampMin = "1.0"))
	float CreepHealthPerUpgrade = 1.10f;

	/** The health multiplier of a creep spawned this many seconds into the siege clock. */
	float GetCreepHealthMultiplierAt(float SiegeSeconds) const;

	/** A flyer in a pack comes in this high over the lane's start (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes", meta = (ClampMin = "0.0", Units = "cm"))
	float LaneAirHeight = 3500.0f;

	/** A pack member that found no free spot keeps trying this long before it is dropped (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes", meta = (ClampMin = "0.0", Units = "s"))
	float LaneSpawnRetrySeconds = 10.0f;

	/** Lane creeps that got past every turret stop at the core and fight it; this is how close to the
	 *  lane's base end a creep counts as having walked it (cm). Copied onto every creep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes", meta = (ClampMin = "0.0", Units = "cm"))
	float LaneEndDistance = 2500.0f;

	/** A carrier on a lane only goes for what is this near it (cm), a bit past its own standoff
	 *  (AKamikazeCarrierDrone::StandoffDistance, 40 m). Copied onto every creep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes", meta = (ClampMin = "0.0", Units = "cm"))
	float LaneCarrierAggroRadius = 6000.0f;

	/** A walker on a lane takes on only what stands within this distance of its lane (cm): a player in
	 *  a camp behind the lane is left alone, a target that walks off past it is dropped. Copied onto
	 *  every walker (USiegeLaneFollower::WalkerLeashRadius). 0 = no leash. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Lanes", meta = (ClampMin = "0.0", Units = "cm"))
	float LaneWalkerLeashRadius = 4000.0f;

	/** True while the waves come down lanes. */
	UFUNCTION(BlueprintPure, Category = "Siege")
	bool IsLaneSiege() const;

	/** The health multiplier a creep spawned now gets from the upgrades so far. */
	UFUNCTION(BlueprintPure, Category = "Siege")
	float GetCreepHealthMultiplier() const;

	/** Minutes since the siege started, the clock the pack counts are read on. */
	UFUNCTION(BlueprintPure, Category = "Siege")
	float GetSiegeMinutes() const;

	// ==================== Endless waves ====================

	/** What the endless phase may spend its budget on. Empty = the siege stops after the authored
	 *  waves. A kind absent from this list costs 1 when the last authored wave is priced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Endless")
	TArray<FSiegeEnemyType> EndlessPool;

	/** Budget of each endless wave over the one before. 1.0 = the same every wave (never ends);
	 *  1.5 doubles every second wave (the author's number, 2026-09-14). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Endless", meta = (ClampMin = "1.0"))
	float EndlessBudgetGrowth = 1.5f;

	/** Budget of the first endless wave. 0 = the last authored wave's cost times the growth, so the
	 *  curve continues from where the author left it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Endless", meta = (ClampMin = "0.0"))
	float FirstEndlessBudget = 0.0f;

	/** Hard cap on what one endless wave may spawn, whatever the budget says. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Endless", meta = (ClampMin = "1"))
	int32 MaxEnemiesPerWave = 40;

	// ==================== Clock ====================

	/** From the siege starting to the first wave (seconds). Time to look around and build. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Clock", meta = (ClampMin = "0.0", Units = "s"))
	float FirstWaveDelay = 0.0f;

	/** From one wave starting to the next (seconds). The last wave being alive does not delay it:
	 *  they stack, and the stack is the pressure. An authored wave's DelayBeforeWave is added. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Clock", meta = (ClampMin = "1.0", Units = "s"))
	float WaveInterval = 20.0f;

	// ==================== Pacing ====================

	/** Send the next wave early when the base goes quiet. The clock stays the ceiling: a player
	 *  away on a raid does not thin the enemy out, so the pile at home keeps growing on the clock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Pacing")
	bool bEarlyWaveOnLull = true;

	/** Quiet = the budget cost of every enemy this director spawned and is still alive is at or
	 *  under this (a carrier costs 1, a shooter 0.25 on L_HomeBase). All of them count, not only the
	 *  ones near the base: a wave still walking in from the fog is not a lull. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Pacing", meta = (ClampMin = "0.0", EditCondition = "bEarlyWaveOnLull"))
	float LullPressureCost = 0.25f;

	/** From the lull to the early wave (seconds). The walk in from the spawn ring comes on top, and
	 *  that walk is most of the time the player has to build. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Pacing", meta = (ClampMin = "0.0", Units = "s", EditCondition = "bEarlyWaveOnLull"))
	float LullWaveDelay = 2.0f;

	/** A wave comes out one enemy at a time, this far apart (seconds). 0 = the whole wave at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Pacing", meta = (ClampMin = "0.0", Units = "s"))
	float SpawnTrickleInterval = 0.75f;

	/** Measuring only: an enemy within this of the director counts as "at the base", and every
	 *  stretch with none is logged as a break ([SIEGE_DEBUG] break at base). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Pacing", meta = (ClampMin = "0.0", Units = "cm"))
	float BaseRingRadius = 6000.0f;

	// ==================== Spawn ====================

	/** Spawn on a ring around this director instead of on placed points: a walker on a random
	 *  reachable spot of the ground ring, a flyer in the widest gap between the flyers already up.
	 *  The placed points stay as the fallback for when the ring finds nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn")
	bool bUseSpawnRing = true;

	/** Distance of the ground ring from the director (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bUseSpawnRing"))
	float GroundRingRadius = 9000.0f;

	/** A walker lands up to this much nearer or farther than GroundRingRadius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bUseSpawnRing"))
	float GroundRingDepth = 1000.0f;

	/** Where on the ground ring walkers may appear: X from, Y to, in degrees counterclockwise seen
	 *  from above, 0 = +X (east), 90 = +Y. (180, 360) is the western half. Empty = the whole ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (EditCondition = "bUseSpawnRing"))
	TArray<FVector2D> GroundRingArcs;

	/** Distance of the air ring from the director (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bUseSpawnRing"))
	float AirRingRadius = 16000.0f;

	/** A flyer appears this high above the ground under it (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bUseSpawnRing"))
	float AirRingHeight = 3500.0f;

	/** Same as GroundRingArcs, for flyers. Empty = the whole ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (EditCondition = "bUseSpawnRing"))
	TArray<FVector2D> AirRingArcs;

	/** No spawn nearer than this to a live enemy or to anything spawned in the last few seconds (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bUseSpawnRing"))
	float MinSpawnSeparation = 500.0f;

	/** Random spots a walker's spawn tries before it falls back to the placed points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (ClampMin = "1", ClampMax = "32", EditCondition = "bUseSpawnRing"))
	int32 SpawnRingAttempts = 8;

	/** A walker's spot must have a full navmesh path to the base's core (or to the director when
	 *  there is no core yet), so nobody is born in a pit or on a ledge. One path query per try. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn", meta = (EditCondition = "bUseSpawnRing"))
	bool bRequirePathToBase = true;

	/** Where the enemy enters when the ring is off or finds nothing. The same markers the arenas
	 *  use, air spawn included. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn")
	TArray<TSoftObjectPtr<AArenaSpawnPoint>> SpawnPoints;

	/** Also take every AArenaSpawnPoint found in the world at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Spawn")
	bool bAutoCollectSpawnPoints = true;

	// ==================== Carriers ====================

	/** At most this many carriers alive at once. A carrier rolled past it is not spawned: its cost
	 *  is split evenly between the living carriers as power (more shield, more drops), so the field
	 *  holds as much carrier as the budget bought, in fewer bodies. 0 = no cap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Carriers", meta = (ClampMin = "0"))
	int32 MaxCarriersAlive = 4;

	/** One clock for every carrier's drops instead of each carrier's own cooldown. Its rate is the
	 *  sum over living carriers of power / SalvoCooldown, and each tick of it goes to one carrier
	 *  that is ready (in position, in sight, target not full), picked by power. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Carriers")
	bool bPaceCarrierSalvos = true;

	/** Shortest gap between two salvos of one carrier (seconds). Past it a strong carrier's extra
	 *  power stops adding drops and only its shield keeps growing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Carriers", meta = (ClampMin = "0.5", Units = "s", EditCondition = "bPaceCarrierSalvos"))
	float MinCarrierSalvoInterval = 4.0f;

	/** X: wave number. Y: kamikaze drones allowed on one player at once (rounded, 1..20). Applied
	 *  to every carrier at each wave start and to each new one. No keys = the carriers' own
	 *  MaxDronesPerTarget. A dive on an undefended core is never capped by this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Carriers")
	FRuntimeFloatCurve DronesPerTargetByWave;

	// ==================== Start ====================

	/** The first dispenser a player builds becomes the base's core and starts the siege
	 *  (Docs/Dispenser_Core_Refinery_Plan_2026-09-22.md). While on, StartTriggers are not listened
	 *  to: putting the base down is the "ready". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Start")
	bool bStartWhenDispenserBuilt = true;

	/** Actors whose overlap by a player starts the siege (a console in the house). Ignored while
	 *  bStartWhenDispenserBuilt is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Start")
	TArray<TSoftObjectPtr<AActor>> StartTriggers;

	/** Start the clock at BeginPlay instead of waiting for a trigger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Start")
	bool bStartOnBeginPlay = false;

	// ==================== Base ====================

	/** Level-placed cores. Empty = every ASiegeCoreBuildable in the world at BeginPlay. A dispenser
	 *  that becomes the core joins them at runtime. When the last core falls the base is lost and
	 *  the clock stops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siege|Base")
	TArray<TSoftObjectPtr<ASiegeCoreBuildable>> Cores;

	// ==================== State ====================

	/** Wave number of the wave most recently started, from 1. 0 before the first. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Siege|State")
	int32 CurrentWave = 0;

	/** Server world time the next wave is due at. See GetSecondsToNextWave. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Siege|State")
	float NextWaveServerTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Siege|State")
	bool bSiegeActive = false;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Siege|State")
	bool bBaseLost = false;

	/** Enemies this director spawned that are still alive. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Siege|State")
	int32 AliveEnemies = 0;

	/** Server world time the siege started at. The lane packs grow on the clock from here. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Siege|State")
	float SiegeStartServerTime = 0.0f;

	UFUNCTION(BlueprintPure, Category = "Siege")
	float GetSecondsToNextWave() const;

	/** True once the authored list is spent. */
	UFUNCTION(BlueprintPure, Category = "Siege")
	bool IsEndless() const { return CurrentWave > AuthoredWaves.Num(); }

	/** Budget an endless wave of this number gets. Authored numbers return their list cost. */
	UFUNCTION(BlueprintPure, Category = "Siege")
	float GetWaveBudget(int32 WaveNumber) const;

	/** DronesPerTargetByWave at this wave, rounded and clamped. -1 = the curve has no keys. */
	UFUNCTION(BlueprintPure, Category = "Siege")
	int32 GetDronesPerTargetForWave(int32 WaveNumber) const;

	/** The carriers this director spawned should drop on its clock, not their own. */
	bool IsPacingCarriers() const { return bPaceCarrierSalvos && bSiegeActive; }

	UPROPERTY(BlueprintAssignable, Category = "Siege|Events")
	FOnSiegeStarted OnSiegeStarted;

	UPROPERTY(BlueprintAssignable, Category = "Siege|Events")
	FOnSiegeWaveStarted OnWaveStarted;

	UPROPERTY(BlueprintAssignable, Category = "Siege|Events")
	FOnSiegeBaseLost OnBaseLost;

	// ==================== Control (server) ====================

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Siege")
	void StartSiege();

	/** Stop the clock. Whatever is alive stays alive. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Siege")
	void StopSiege();

	/** Skip the wait: the next wave starts now and the clock restarts from it. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Siege")
	void CallNextWaveNow();

	/** A player's building has just been put down. The first dispenser on this map becomes the
	 *  core and starts the siege, when bStartWhenDispenserBuilt. Server only. */
	void NotifyBuildablePlaced(ABuildableActor* Building);

	/** The director of this world, or null. One per level. */
	static ASiegeDirector* Get(const UWorld* World);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	void CollectSpawnPoints();
	void CollectCores();
	void RegisterStartTriggers();

	/** Watch Core for its fall. */
	void RegisterCore(ABuildableActor* Core);

	/** A core in the list is still standing. */
	bool HasStandingCore() const;

	void ScheduleNextWave(float Delay);
	void StartWave();
	void SpawnAuthoredWave(const FArenaWave& Wave);
	void SpawnEndlessWave(int32 WaveNumber);

	/** Put one enemy in the trickle queue. */
	void EnqueueSpawn(TSubclassOf<AShooterNPC> NPCClass);

	/** Spawn now. False when it was not spawned: no spot found, or a carrier past the cap (whose
	 *  cost then went to the living carriers). */
	bool SpawnOne(TSubclassOf<AShooterNPC> NPCClass);
	AArenaSpawnPoint* PickSpawnPoint(TSubclassOf<AShooterNPC> NPCClass);

	// ---- Lanes ----

	/** One pack on every open lane at once (bStaggerLanes off). */
	void SpawnLaneWave(int32 WaveNumber, float WaveStart);

	/** Put Member of a pack down on Lane now. False = no free spot this time, try again later. */
	bool SpawnOnLane(TSubclassOf<AShooterNPC> NPCClass, ASiegeLane* Lane, int32 Member, float StartDistance);

	/** Where a lane creep enters: its pack slot, on the ground (a walker) or LaneAirHeight above it. */
	bool FindLaneSpawn(TSubclassOf<AShooterNPC> NPCClass, const ASiegeLane* Lane, int32 Member, float StartDistance, FTransform& OutTransform);

	/** CreepLaneSpeed, or the slowest walker of LanePack when it is 0 (cm/s). */
	float GetCreepLaneSpeed() const;

	/** The arrival schedule (bStaggerLanes): set it up at the start, then keep the lane queue filled
	 *  with every pack due to leave within the longest walk from now. */
	void StartLanePlan();
	void TickLanePlan(float Now);

	/** A lane creep of a later wave than any so far came out: that wave has started. */
	void NoteLaneWaveStarted(int32 Wave);

	FLanePlanCursor LanePlan;
	bool bLanePlanActive = false;

	/** Work the lane queue: spawn what can go down, drop what waited too long. */
	void TickLaneQueue(float Now);

	/** The spawn ring's choices, shared by the ring and the lanes: the new NPC is the director's to
	 *  count, pace and send at the base. */
	void AdoptSpawned(AShooterNPC* NPC, TSubclassOf<AShooterNPC> NPCClass, const FVector& Where);

	struct FLaneSpawn
	{
		TSubclassOf<AShooterNPC> NPCClass;
		TWeakObjectPtr<ASiegeLane> Lane;
		int32 Member = 0;
		int32 Wave = 0;
		/** When it leaves its lane's start (server time). Past = it appears where it has walked to. */
		float LeaveAt = 0.0f;
		/** First try at a spot; the retry clock runs from it. */
		float FirstTryAt = -1.0f;
	};
	TArray<FLaneSpawn> LaneQueue;

	// ---- Spawn ring ----

	bool FindRingSpawn(TSubclassOf<AShooterNPC> NPCClass, FTransform& OutTransform);
	bool FindGroundRingSpawn(TSubclassOf<AShooterNPC> NPCClass, FTransform& OutTransform);
	bool FindAirRingSpawn(TSubclassOf<AShooterNPC> NPCClass, FTransform& OutTransform);

	/** Nothing solid in a body of this size here, and no live enemy or fresh spawn too near. */
	bool IsSpawnSpotFree(const FVector& Location, float Radius, float HalfHeight) const;

	/** The ground under this XY, traced from far above and below the director. */
	bool TraceGroundAt(const FVector& FlatLocation, FVector& OutGround) const;

	/** Bearing of a location around this director, degrees 0..360, 0 = +X. */
	float BearingOf(const FVector& Location) const;

	// ---- Carriers ----

	/** Live carriers this director spawned. */
	void GatherLiveCarriers(TArray<AKamikazeCarrierDrone*>& OutCarriers) const;

	/** A carrier past MaxCarriersAlive: hand its cost to the living ones. True = absorbed, do not spawn. */
	bool TryAbsorbCarrierOverflow(TSubclassOf<AShooterNPC> NPCClass);

	void ApplyDronesPerTarget(AKamikazeCarrierDrone* Carrier) const;

	// ---- Pulse ----

	/** Four times a second while the siege runs: the trickle, the lull check, the carrier clock,
	 *  and the break log. */
	void Pulse();
	void TickSpawnQueue(float Now);
	void TickLull(float Now);
	void TickCarrierPacing(float DeltaTime);
	void TickBreakLog(float Now);

	/** Budget cost of every live enemy this director spawned. */
	float GetAliveCost() const;

	/** An authored wave's DelayBeforeWave, 0 for an endless one. */
	float GetExtraDelayBefore(int32 WaveNumber) const;

	float GetServerNow() const;

	/** Budget cost of one of this class: its EndlessPool entry, else 1. */
	float CostOf(TSubclassOf<AShooterNPC> NPCClass) const;
	float CostOfWave(const FArenaWave& Wave) const;

	void RefreshAliveCount();

	UFUNCTION()
	void OnEnemyDied(AShooterNPC* DeadNPC);

	UFUNCTION()
	void OnCoreChanged(ABuildableActor* Core);

	UFUNCTION()
	void OnStartTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	TArray<TWeakObjectPtr<AArenaSpawnPoint>> ResolvedSpawnPoints;
	TArray<TWeakObjectPtr<ABuildableActor>> ResolvedCores;
	TArray<TWeakObjectPtr<AShooterNPC>> Alive;
	FTimerHandle WaveTimer;
	int32 NextSpawnPointIndex = 0;

	/** Enemies of the waves so far that have not come out yet, oldest first. */
	TArray<TSubclassOf<AShooterNPC>> SpawnQueue;
	float NextTrickleTime = 0.0f;

	/** Where things were put down lately, so the next one keeps its distance. */
	struct FRecentSpawn
	{
		FVector Location = FVector::ZeroVector;
		float Time = 0.0f;
		bool bAir = false;
	};
	TArray<FRecentSpawn> RecentSpawns;

	FTimerHandle PulseTimer;
	float LastPulseTime = -1.0f;

	/** Salvos owed by the shared carrier clock, held at one while nobody is ready. */
	float PacedSalvoCredit = 0.0f;

	bool bBaseQuiet = false;
	float BaseQuietSince = 0.0f;

	bool bWarnedNoBaseNav = false;
};
