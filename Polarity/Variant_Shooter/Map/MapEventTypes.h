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
	 *  loot, except that no amount of standing in it hands it over. It is broken instead, by taking
	 *  down the banner standing in it (ABannerActor). The director refuses capture on this role, so
	 *  the rule is one line in one place. */
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

	/** Garrisons are spawned once per run, on the first load of the point. A sublevel that unloads
	 *  and loads again must not refill it. */
	UPROPERTY(BlueprintReadOnly, Category = "POI")
	bool bGarrisonSpawned = false;

	/** True while an actor for this point exists in a loaded level. When false the state is frozen:
	 *  nobody counts who stands where, because nobody is standing anywhere. */
	UPROPERTY(BlueprintReadOnly, Category = "POI")
	bool bLoaded = false;

	/** Somebody broke the thing standing in the middle of this place. What that cost is the point's
	 *  business - a headquarters drops to its weakened squads, anywhere else spills its loot - but
	 *  the fact is remembered here, where it survives the point being streamed out. */
	UPROPERTY(BlueprintReadOnly, Category = "POI")
	bool bBannerBroken = false;

	// ==================== What each side knows ====================
	//
	// Who OWNS a place is public: there is a flag standing in it and anybody walking past reads it.
	// How many are standing in it is not. So a faction plans on what it last had eyes on, and that
	// memory goes stale - which is the whole reason its plans can be wrong, and the reason watching
	// them be wrong is interesting rather than a bug.

	/** Live count of each side standing here. A side always knows its own strength. */
	UPROPERTY(BlueprintReadOnly, Category = "POI")
	int32 PresentA = 0;

	UPROPERTY(BlueprintReadOnly, Category = "POI")
	int32 PresentB = 0;

	/** What faction A believes B has here, and the world time it last actually looked. */
	UPROPERTY(BlueprintReadOnly, Category = "POI")
	int32 KnownEnemyForA = 0;

	UPROPERTY(BlueprintReadOnly, Category = "POI")
	float KnownEnemyForATime = -100000.0f;

	UPROPERTY(BlueprintReadOnly, Category = "POI")
	int32 KnownEnemyForB = 0;

	UPROPERTY(BlueprintReadOnly, Category = "POI")
	float KnownEnemyForBTime = -100000.0f;


	/** When each side last had somebody STANDING here, as opposed to merely being told about it.
	 *
	 *  Split from the intel clock the day scouts arrived, and the reason is the stagnation bonus:
	 *  it grows a point's value the longer a faction has not been there, and it used to read the
	 *  intel clock because looking and going were the same act. They are not any more. A scout who
	 *  glances at a corner of the map would otherwise reset the very number that was about to send
	 *  an assault there, and the map would quietly shrink back to three points nobody leaves. */
	/** Сколько крови стоило этой стороне это место, с затуханием по времени.
	 *
	 *  Штаб раньше не помнил ничего: план считался по текущим силам врага, а что на точке уже
	 *  положили два отряда - нет. Так и получается мясорубка, в которую подкрепления идут ровным
	 *  ручьём, пока не кончатся. Число растёт на каждого убитого и само рассасывается, поэтому
	 *  место дорожает после бойни и снова дешевеет, когда там давно тихо. */
	UPROPERTY(BlueprintReadOnly, Category = "War")
	float BloodForA = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "War")
	float BloodForATime = -100000.0f;

	UPROPERTY(BlueprintReadOnly, Category = "War")
	float BloodForB = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "War")
	float BloodForBTime = -100000.0f;

	UPROPERTY(BlueprintReadOnly, Category = "War")
	float VisitedByATime = -100000.0f;

	UPROPERTY(BlueprintReadOnly, Category = "War")
	float VisitedByBTime = -100000.0f;};

/** What a faction wants doing with a place next. */
UENUM(BlueprintType)
enum class EFactionAction : uint8
{
	/** March on somewhere this faction does not hold. */
	Attack UMETA(DisplayName = "Attack"),

	/** Send help to somewhere it holds and is losing. A faction that only ever attacks loses the
	 *  map behind its own advance, which reads as an army with no idea what it owns. */
	Reinforce UMETA(DisplayName = "Reinforce")
};

/** One line of a faction's plan: a place, what to do with it, and why it scored where it did.
 *
 *  A LINE of a plan rather than a stored plan, because a stored order of conquest goes stale the
 *  moment anybody dies. The plan is rebuilt from the map every time a headquarters is ready to send
 *  somebody, and the ordering IS the plan; keeping the score and the reason on the line is what
 *  makes it possible to answer "why did they go there" without guessing. */
USTRUCT(BlueprintType)
struct POLARITY_API FFactionOrder
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Plan")
	EFactionAction Action = EFactionAction::Attack;

	UPROPERTY(BlueprintReadOnly, Category = "Plan")
	FName PoiTag;

	UPROPERTY(BlueprintReadOnly, Category = "Plan")
	FVector Location = FVector::ZeroVector;

	/** Higher is more wanted. Comparable only within one call: the scale is relative. */
	UPROPERTY(BlueprintReadOnly, Category = "Plan")
	float Score = 0.0f;

	/** Which team holds the place right now, so a caller can tell an assault from an occupation. */
	UPROPERTY(BlueprintReadOnly, Category = "Plan")
	uint8 HeldBy = 255;

	/** Human-readable reason the line scored what it did. Debug only; never branch on it. */
	UPROPERTY(BlueprintReadOnly, Category = "Plan")
	FString Reason;

	/** Odds the faction gives itself here, 0..1, from what it believes about both sides. Kept on
	 *  the line because "we went and lost" and "we went at odds we knew were bad" are different
	 *  bugs and the log has to tell them apart. */
	UPROPERTY(BlueprintReadOnly, Category = "Plan")
	float Chance = 0.0f;

	/** True when the line cleared the worth threshold. A plan keeps the lines it REJECTED: the
	 *  interesting question is usually why the faction sat still, and a list of what it looked at
	 *  and turned down answers it. */
	UPROPERTY(BlueprintReadOnly, Category = "Plan")
	bool bWorthIt = false;
};
