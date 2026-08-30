// MapEventTypes.h
// The words the map event layer is written in: what a point on the map can be, who holds it, what
// it pays, and which phase the run is in.
//
// One file for the vocabulary, so the director, the points, the headquarters and the extraction
// routes cannot each invent their own. Design source: the goals seminar (artifact "Kuda idyot
// igrok", 2026-08-27), sections 09-12. Numbers there are defaults here, not laws: every one of them
// is EditAnywhere on the actor that uses it.

#pragma once

#include "CoreMinimal.h"
// Sides come from the one place that already owns them: PolarityTeams::Players / FactionA /
// FactionB / Neutral, next to the hostility rule they have to agree with.
#include "AI/PolarityTeams.h"
#include "MapEventTypes.generated.h"

class AInventoryPickup;

/** What a point of interest is for. The map carries many of these; three carry missions, two are
 *  headquarters, one is the final. Everything else is plain loot. */
UENUM(BlueprintType)
enum class EPoiRole : uint8
{
	/** Cheap, safe, uncontested loot that costs time. Deliberately worse than a contested point:
	 *  if the safe option pays the same it eats the whole run. */
	Plain UMETA(DisplayName = "Plain loot"),

	/** Carries one of the three missions. Its window is the war, not a clock: it closes when a
	 *  faction finishes taking the point. */
	Mission UMETA(DisplayName = "Mission"),

	/** A faction headquarters, which is an AFactionHq: a point like any other, with a garrison and
	 *  loot, except that no amount of standing in it hands it over. It is broken instead, one
	 *  function at a time (ASabotageTarget). The director refuses capture on this role, so the rule
	 *  is one line in one place. */
	Headquarters UMETA(DisplayName = "Headquarters"),

	/** Where the run ends. Capture, then hold, then the route to the exit is announced. */
	Final UMETA(DisplayName = "Final")
};

/** The thing on a point that is worth crossing a battle for. Every one of them is produced by the
 *  war rather than placed in a box, is visible from outside, and expires: the three conditions from
 *  the seminar, section 09. Lifetime is what separates fast points from slow ones, and that is what
 *  the blockout of a point gets designed around. */
UENUM(BlueprintType)
enum class EPoiPrize : uint8
{
	None,

	/** Carried by an elite in the garrison. The window shuts seconds after they die, when their own
	 *  side picks it up. */
	TrophyWeapon UMETA(DisplayName = "Trophy weapon"),

	/** A vehicle one faction knocked out. Repair crews, or the fire, take it back within a minute. */
	WreckedVehicle UMETA(DisplayName = "Wrecked vehicle"),

	/** The power of the point. The slow prize: it lasts until somebody finishes the capture, and it
	 *  is the only one worth standing on a point for. */
	PointPower UMETA(DisplayName = "Point power")
};

/** What a mission asks for. From the catalogue in section 04 of the seminar: the four archetypes
 *  the verbs already in the project can carry. */
UENUM(BlueprintType)
enum class EMissionKind : uint8
{
	/** Kill one named target inside the fight. */
	Elimination,

	/** Break one thing of theirs and leave. */
	Sabotage,

	/** Pick a thing up here and carry it there. */
	Delivery,

	/** Be on this spot when the clock runs out. */
	Hold
};

/** Which of a headquarters' functions a sabotage target is. Breaking one takes that function away
 *  from the faction for the rest of the run. */
UENUM(BlueprintType)
enum class ESabotageKind : uint8
{
	/** No more sorties leave this headquarters. */
	Reinforcements,

	/** Loadouts marked as vehicles stop appearing in its sorties. */
	Vehicles,

	/** Its points stop generating power, which is what the slow prize is made of. */
	Power
};

/** Where the run is. One map, so this is a straight line with no branches. */
UENUM(BlueprintType)
enum class ERunPhase : uint8
{
	/** Before the launch point has put anybody on the ground. */
	NotStarted,

	/** The open half of the run: missions, loot, headquarters, the triangle of time. */
	Open,

	/** The final point is live and both armies are converging on it. Missions still standing are
	 *  still worth doing, but the clock is now visible. */
	FinalOpen,

	/** The team has taken the final and is holding it. Nothing is announced yet. */
	HoldingFinal,

	/** The route is out and the chase is on. */
	Extraction,

	/** Somebody left, or nobody did. */
	Ended
};

/**
 * What a mission pays.
 *
 * Missions pay in conditions of the final, not in power, which is the whole reason a run with one
 * mission is still winnable and a run with three is easier rather than shorter (seminar, section
 * 10). The director sums these across every mission completed.
 */
USTRUCT(BlueprintType)
struct POLARITY_API FFinalConditions
{
	GENERATED_BODY()

	/** Waves at the final. Negative takes waves away; that is the normal direction for a reward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Final")
	int32 WaveDelta = 0;

	/** Seconds the enemy armies arrive later by. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Final")
	float ArrivalDelaySeconds = 0.0f;

	/** How much better the way in is: 0 none, 1 a side door, 2 a side door and the roof. Read by the
	 *  final point when it decides which of its entrances start open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Final", meta = (ClampMin = "0", ClampMax = "3"))
	int32 EntryQuality = 0;

	void Add(const FFinalConditions& Other)
	{
		WaveDelta += Other.WaveDelta;
		ArrivalDelaySeconds += Other.ArrivalDelaySeconds;
		EntryQuality += Other.EntryQuality;
	}
};

/** One pile of something on a point. Money is the interesting one: it is what turns a cell of the
 *  grid into a decision (Docs/Inventory_Slot_Contract_2026-08-28.md). */
USTRUCT(BlueprintType)
struct POLARITY_API FPoiLootEntry
{
	GENERATED_BODY()

	/** An AInventoryPickup subclass: currency, ammo, an attachment, an ability upgrade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot")
	TSubclassOf<AInventoryPickup> PickupClass = nullptr;

	/** How many of them this point puts down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "1", ClampMax = "20"))
	int32 Count = 1;

	/** Scattered within this radius of the point (cm). Zero uses the point's own influence radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "0.0"))
	float ScatterRadius = 0.0f;

	/** Counts against the money budget in the director's audit. Tick it on currency piles and on
	 *  nothing else, or the audit reports the wrong number and the dilemma gets mistuned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot")
	bool bIsMoney = false;
};

/**
 * What the director remembers about a point.
 *
 * It lives on the director rather than on the point actor on purpose: points sit in streamed
 * sublevels, and a sublevel that unloads must not take the state of the war with it. The actor is
 * the presence of the point in the world; this struct is the point itself.
 */
USTRUCT(BlueprintType)
struct POLARITY_API FPoiWarState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "POI")
	FName PoiTag;

	UPROPERTY(BlueprintReadOnly, Category = "POI")
	EPoiRole Role = EPoiRole::Plain;

	/** Last known world position, kept so a streamed-out point can still be a destination. */
	UPROPERTY(BlueprintReadOnly, Category = "POI")
	FVector Location = FVector::ZeroVector;

	/** Who holds it. PolarityTeams::Neutral until somebody finishes taking it. */
	UPROPERTY(BlueprintReadOnly, Category = "POI")
	uint8 ControllingTeam = 255;

	/** How far the current attacker has got, 0..1. Reaching 1 flips ControllingTeam and shuts any
	 *  mission window on this point: that is what "the window is the war" means in code. */
	UPROPERTY(BlueprintReadOnly, Category = "POI")
	float CaptureProgress = 0.0f;

	/** Which team the progress belongs to. Neutral when nobody is pushing. */
	UPROPERTY(BlueprintReadOnly, Category = "POI")
	uint8 CapturingTeam = 255;

	/** Two sides present and neither winning. This is the state a mission point has to be in for the
	 *  mission to be worth anything; an uncontested point makes the whole war a backdrop. */
	UPROPERTY(BlueprintReadOnly, Category = "POI")
	bool bContested = false;

	UPROPERTY(BlueprintReadOnly, Category = "POI")
	bool bMissionWindowOpen = false;

	UPROPERTY(BlueprintReadOnly, Category = "POI")
	bool bMissionCompleted = false;

	/** The window shut before the team got there. Not a failure to punish, just a door that closed:
	 *  the run stays winnable on one mission out of three. */
	UPROPERTY(BlueprintReadOnly, Category = "POI")
	bool bMissionExpired = false;

	/** Loot is put down once per run, on the first load of the point. A sublevel that unloads and
	 *  loads again must not refill it. */
	UPROPERTY(BlueprintReadOnly, Category = "POI")
	bool bLootSpawned = false;

	/** Garrisons follow the same rule as loot, for the same reason. */
	UPROPERTY(BlueprintReadOnly, Category = "POI")
	bool bGarrisonSpawned = false;

	/** True while an actor for this point exists in a loaded level. When false the state is frozen:
	 *  nobody counts who stands where, because nobody is standing anywhere. */
	UPROPERTY(BlueprintReadOnly, Category = "POI")
	bool bLoaded = false;

	/** How much money this point put on the floor, for the budget audit. */
	UPROPERTY(BlueprintReadOnly, Category = "POI")
	int32 MoneyStacksPlaced = 0;
};
