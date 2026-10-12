// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "GenericTeamAgentInterface.h"
#include "ShooterAIController.generated.h"

class UStateTreeAIComponent;
class UAIPerceptionComponent;
class UPolarityPathFollowingComponent;
struct FAIStimulus;
class AShooterNPC;
class ABuildableActor;

DECLARE_DELEGATE_TwoParams(FShooterPerceptionUpdatedDelegate, AActor*, const FAIStimulus&);
DECLARE_DELEGATE_OneParam(FShooterPerceptionForgottenDelegate, AActor*);

/** Who is asking this NPC to look at something.
 *
 *  "What is this NPC fighting" had several answers before this: perception wrote one, the arena
 *  wrote another, the decoy wrote a third, and which of them stuck depended on call order. The decoy
 *  had to be made to work by teaching SetCurrentTarget to REFUSE everybody else while a distraction
 *  ran, which works exactly once: the second mechanic with a priority would have brought its own
 *  refusal, and the two would have argued.
 *
 *  So the order is written down instead. Every source owns one slot, the highest live slot wins, and
 *  there is one place that writes the answer.
 *
 *  Numbers are spaced so a new source can be slotted between two existing ones without renumbering
 *  the file. Equal numbers are legal and mean "last writer wins", which is what Perception and
 *  Script did before this existed and must keep doing (see FTargetIntent::SetTime).
 *
 *  Plain enum rather than UENUM on purpose: two sources deliberately share a value, which a UEnum
 *  cannot represent cleanly, and nothing in Blueprint needs to name these. */
enum class ETargetIntentSource : uint8
{
	/** What the NPC can see. The ordinary case, and the only one most fights ever use. */
	Perception = 0,

	/** Scripted aggro: arena setup, tutorials, sequences. Deliberately the SAME priority as
	 *  perception rather than above it, because that is what it was before. Raising it is a design
	 *  decision (should a scripted target survive the enemy seeing somebody else?) and belongs to
	 *  the author, not to a refactor. */
	Script = 0,

	/** Provocation: the Tank's shotgun, or anything else that buys attention through UThreatComponent.
	 *  Nothing writes this yet. It is named here so that when it arrives it lands in the order that
	 *  was reasoned about, rather than wherever it is convenient on the day. */
	Threat = 40,

	/** The coordinator redistributing pressure across the team. Nothing writes this yet either: today
	 *  the coordinator keeps its own answer and the behaviour tree never reads it. Wiring it up is
	 *  what finally makes those two agree. */
	Coordinator = 60,

	/** A hostile sentry turret this NPC is taking cover from. Outranks perception deliberately: an
	 *  NPC under a turret's fire plays the corner even while it can also see a player, because the
	 *  turret is the thing pinning it. Written by the NPC's own turret-cover machine
	 *  (AShooterNPC::TickTurretCover), which is also the only reader that starts and ends it. */
	Turret = 80,

	/** A decoy. Outranks everything because that is the whole mechanic: a fully charged prop should
	 *  hold a room even though every NPC in it can plainly see a player. */
	Distraction = 100
};

/** One source's standing request. */
struct FTargetIntent
{
	ETargetIntentSource Source = ETargetIntentSource::Perception;

	/** Weak on purpose: a decoy prop can be destroyed mid-pull and an arena can tear down its
	 *  actors, and neither should leave this controller holding the thing alive or pointing at
	 *  freed memory. An intent whose target has gone simply stops winning. */
	TWeakObjectPtr<AActor> Target;

	/** World time this stops counting. Zero means it stands until cleared. */
	float ExpiryTime = 0.0f;

	/** World time it was set. Breaks ties between equal priorities in favour of the more recent
	 *  writer, which is how perception and the arena behaved when they simply overwrote each other's
	 *  value. Without it, converting them to intents would silently freeze whichever source happened
	 *  to be declared first in the enum. */
	float SetTime = 0.0f;

	bool bSet = false;
};

// Blueprint-compatible perception events
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnemySpotted, AActor*, SpottedEnemy, FVector, LastKnownLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyLost, AActor*, LostEnemy);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTeamPerceptionReceived, AActor*, ReportedEnemy, FVector, LastKnownLocation);

/**
 *  Simple AI Controller for a first person shooter enemy
 */
UCLASS(abstract)
class POLARITY_API AShooterAIController : public AAIController
{
	GENERATED_BODY()

	/** Runs the behavior StateTree for this NPC */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UStateTreeAIComponent* StateTreeAI;

	/** Detects other actors through sight, hearing and other senses */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UAIPerceptionComponent* AIPerception;

protected:

	/** Team tag for pawn friend or foe identification */
	UPROPERTY(EditAnywhere, Category = "Shooter")
	FName TeamTag = FName("Enemy");

	/** Team ID for GenericTeamAgentInterface (all enemies share the same team) */
	UPROPERTY(EditAnywhere, Category = "Shooter|Team Perception")
	FGenericTeamId TeamId = FGenericTeamId(1);

	/** Radius within which to notify teammates about detected enemies */
	UPROPERTY(EditAnywhere, Category = "Shooter|Team Perception", meta = (ClampMin = "0.0"))
	float TeamPerceptionRadius = 2000.0f;

	/** Whether to broadcast enemy detections to teammates */
	UPROPERTY(EditAnywhere, Category = "Shooter|Team Perception")
	bool bSharePerceptionWithTeam = true;

	/** The resolved answer: what this NPC is fighting right now. Derived from Intents by
	 *  ResolveTargetIntents and written nowhere else, so there is exactly one line in this class
	 *  that decides it. Server-side state, like everything the AI decides. */
	TObjectPtr<AActor> TargetEnemy;

	/** One standing request per source that has ever set one. At most a handful, walked linearly. */
	TArray<FTargetIntent> Intents;

	/** Recompute TargetEnemy from Intents and push the result into focus.
	 *
	 *  Drops expired and dead-target requests as it goes, so nothing else has to remember to prune.
	 *  Called after every write, and also from Tick, because an intent can expire without anybody
	 *  touching this controller. */
	void ResolveTargetIntents();

	/** Find this source's slot, or null. */
	FTargetIntent* FindIntent(ETargetIntentSource Source);
	const FTargetIntent* FindIntent(ETargetIntentSource Source) const;

public:

	/** Called when an AI perception has been updated. StateTree task delegate hook */
	FShooterPerceptionUpdatedDelegate OnShooterPerceptionUpdated;

	/** Called when an AI perception has been forgotten. StateTree task delegate hook */
	FShooterPerceptionForgottenDelegate OnShooterPerceptionForgotten;

	// ==================== Blueprint Perception Events ====================

	/** Called when this AI spots an enemy (via Sight sense) */
	UPROPERTY(BlueprintAssignable, Category = "AI|Perception")
	FOnEnemySpotted OnEnemySpotted;

	/** Called when this AI loses sight of an enemy */
	UPROPERTY(BlueprintAssignable, Category = "AI|Perception")
	FOnEnemyLost OnEnemyLost;

	/** Called when this AI receives a team perception about an enemy from a teammate */
	UPROPERTY(BlueprintAssignable, Category = "AI|Perception")
	FOnTeamPerceptionReceived OnTeamPerceptionReceived;

public:

	/** Constructor — sets PolarityPathFollowingComponent as default */
	AShooterAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	/** Called when play begins */
	virtual void BeginPlay() override;

	/** Only here to let intents expire without anybody writing to this controller. Walks at most a
	 *  handful of entries and does nothing at all when none of them has a deadline. */
	virtual void Tick(float DeltaTime) override;

	/** Pawn initialization */
	virtual void OnPossess(APawn* InPawn) override;

protected:

	/** Called when the possessed pawn dies */
	UFUNCTION()
	void OnPawnDeath(AShooterNPC* DeadNPC);

public:

	// ==================== Target intents ====================
	// The only way to change what this NPC fights. Nobody refuses anybody any more: a source states
	// what it wants and how long it wants it, and the order in ETargetIntentSource decides.

	/** State this source's request. Duration <= 0 means it stands until cleared, which is what
	 *  perception and the arena want; a decoy passes its remaining lifetime.
	 *
	 *  Calling it again for the same source replaces that source's request rather than stacking, so
	 *  refreshing a decoy every tick is normal and cheap. Target may be null, which is the same as
	 *  clearing this source. */
	void SetTargetIntent(ETargetIntentSource Source, AActor* Target, float Duration = 0.0f);

	/** Withdraw this source's request. Harmless if it never had one. */
	void ClearTargetIntent(ETargetIntentSource Source);

	/** What this source is currently asking for, or null. */
	AActor* GetTargetIntent(ETargetIntentSource Source) const;

	/** Sets the targeted enemy. Shorthand for a standing Perception intent, kept because that is
	 *  what almost every caller means and the name reads better at the call site. */
	void SetCurrentTarget(AActor* Target);

	/** Clears the targeted enemy, i.e. withdraws the Perception intent. Note that this no longer
	 *  necessarily clears the ANSWER: if something higher is still asking, that request wins, which
	 *  is exactly the behaviour the decoy needed and used to get through a refusal. */
	void ClearCurrentTarget();

	/** Returns the targeted enemy */
	AActor* GetCurrentTarget() const { return TargetEnemy; };

	// ==================== Knowledge ====================
	// The NPC acts on what it knows, not on what is (Docs/Handoff_AI_TargetKnowledge_2026-10-12.md).
	//
	// Three things reach it. SIGHT: an enemy inside its view cone with a clear line from the eyes;
	// that is a place and a name, and after CalloutDelay the whole side has it (the radio). SOUND: a
	// shot that is not its own side's, heard inside the gun's ShotNoiseRange; that is a place and
	// no name, and it turns heads. A HIT: a direction, and a place only when the shot was heard too.
	//
	// The target is an actor and an actor always has a position; what the NPC has is the place it
	// THINKS the target is. While the target is in its view, or in the view of somebody of its side,
	// that is where the target stands. Once nobody sees it, the place stays where it was last seen.
	//
	// One per NPC and kept here, next to the target, so the tasks, the cover search and the gun all
	// ask the same thing. Switch: polarity.ai.targetknowledge (0 is the old rule).

	/** Where this NPC thinks its current target is. Zero with no target. */
	FVector GetBelievedTargetLocation() const;

	/** Where this NPC thinks Enemy is, whoever he is: in its own view or its side's, where he
	 *  stands; its target, the believed place; anybody else, what the side last knew. False when
	 *  it knows nothing of him. Everything that hides from, walks round or aims off an enemy asks
	 *  this and not the actor. */
	bool GetKnownPlaceOf(const AActor* Enemy, FVector& OutPlace) const;

	/** True while the target is in view of this NPC or of somebody on its side. */
	bool IsTargetLocationKnown() const { return bTargetLocationKnown; }

	/** Seconds since anybody of the side last saw the target, zero while it is in view. */
	float GetSecondsTargetUnknown() const;

	/** How far the target really is from where this NPC thinks it is, below zero with no target.
	 *  For the record of a fight and for nothing else: behaviour must not read it. */
	float GetTargetLocationError() const;

	/** Focus on the target looks at where it is thought to be: a gun does not follow somebody
	 *  through a wall. Every SetFocus(Target) in the tasks goes through this. */
	virtual FVector GetFocalPointOnActor(const AActor* Actor) const override;

	/** Inside the cone this NPC looks out of. Flat: nobody is hidden by standing higher. */
	bool IsInViewCone(const FVector& Point) const;

	/** May this NPC act on a clear line to Actor? Its target: while it knows where the target is.
	 *  Anybody else: while he is inside the cone. A thing that is not a pawn: always.
	 *  AShooterNPC::HasLineOfSightTo asks this first, so no task shoots somebody it has not seen. */
	bool Notices(const AActor* Actor) const;

	/** Has somebody hostile in view right now. */
	bool SeesAnyEnemy() const { return SeenEnemies.Num() > 0; }

	/** Turned towards a sound or a shout, and not yet seen who. */
	bool IsLookingAround() const { return bLookingAround; }

	/** A shot was fired at Location and carries Range. Everybody in reach who is not of the
	 *  shooter's side hears it and knows the place; the nearest few with nobody in view turn to it.
	 *  Called by the gun, so it does not depend on a hearing sense being set up in a blueprint. */
	static void ReportShotNoise(const UObject* WorldContext, APawn* Shooter, const FVector& Location, float Range);

	/** This NPC was hit by Source. It learns a direction; with the shot heard, the place. */
	void NoteHitFrom(AActor* Source, float Damage);

	/** Turn to Place for Seconds, or until somebody is in view. */
	void LookTowards(const FVector& Place, float Seconds);

	/** One of the side walks to the place the target was lost at and looks round from there, the
	 *  rest keep their guns on it. True while this NPC is the one and this owns its legs: the task
	 *  that called must then leave them alone. @see UFactionContactMemory::TryClaimSearch */
	bool TickSearchForTarget(bool bAllowed);

protected:

	/** Refresh everything above. On a short clock rather than every tick: it costs line traces. */
	void UpdateKnowledge(float DeltaTime);

	/** Who is in the cone with a clear line, and the calls to the side that follow from it. */
	void UpdateSeenEnemies(float Now);

	/** Whom to fight, of those the side knows of. @see ETargetIntentSource::Coordinator */
	void UpdateTargetChoice(float Now);

	/** Eyes to body, with smoke and a cloak counted. */
	bool SeesWithOwnEyes(const AActor* Target) const;

	/** The nearest Count of this side with nobody in view turn to Place. */
	void TurnMatesTowards(const FVector& Place, int32 Count);

	/** Say it on the radio: it takes a moment, and a dead man does not finish the sentence (the
	 *  controller dies with its pawn and the timer with the controller). */
	void CallOutLater(float Delay, TFunction<void()> Call);

	void EndSearchForTarget();

	struct FSeenEnemy
	{
		TWeakObjectPtr<AActor> Enemy;
		float FirstSeenTime = 0.0f;
		FVector LastLocation = FVector::ZeroVector;
		bool bCalledOut = false;
		bool bSeenThisPass = false;
	};

	/** A foot soldier: has a cover finder. Only it is held to the knowledge rules. Set on possess. */
	bool bInfantry = false;

	/** Hostile pawns in view right now. A handful. */
	TArray<FSeenEnemy> SeenEnemies;

	struct FHurtRecord
	{
		TWeakObjectPtr<AActor> Source;
		float Damage = 0.0f;
		float Time = 0.0f;
	};

	/** Who hurt this NPC lately, one entry each. Read by the choice of target and forgotten there. */
	TArray<FHurtRecord> HurtBy;

	/** Who BelievedTargetLocation is about. A new target starts from what the side knows of it. */
	TWeakObjectPtr<AActor> BelievedTarget;

	FVector BelievedTargetLocation = FVector::ZeroVector;

	/** World time BelievedTargetLocation was true at, below zero while there is none. */
	float BelievedTime = -1.0f;

	bool bSeesTargetMyself = false;

	bool bTargetLocationKnown = false;

	float KnowledgeTimer = 0.0f;

	/** The last shot heard that was not this side's: whose, where, when. Whose is kept only to tie
	 *  a hit to it; nothing else may read it, the listener does not know who fired. */
	TWeakObjectPtr<AActor> HeardShooter;
	FVector HeardPlace = FVector::ZeroVector;
	float HeardTime = -1000.0f;

	bool bLookingAround = false;
	FVector LookPlace = FVector::ZeroVector;
	float LookUntil = 0.0f;

	float NextHitCalloutTime = 0.0f;

	float TargetChoiceTimer = 0.0f;
	TWeakObjectPtr<AActor> TargetContender;
	int32 ContenderLeads = 0;

	bool bSearchingForTarget = false;
	bool bSearchArrived = false;
	FVector SearchGoal = FVector::ZeroVector;
	float SearchLegUntil = 0.0f;

public:

	// ==================== Distraction ====================
	// A decoy is not something this NPC senses; it is something told to it. The coordinator owns the
	// decision (it is the one place that knows every registered NPC and who each is fighting) and
	// this is where it lands, because the behaviour tree reads the CONTROLLER's target, not the
	// coordinator's — the same route AArenaManager uses when it makes an arena aggro.
	//
	// These are now a thin layer over the Distraction intent. They stay as named functions because
	// "distract this NPC for three seconds" is what the caller means, and because the coordinator
	// already speaks in these terms.

	/** Look at Decoy and hold there for Seconds. Refreshing it with a new deadline is normal: the
	 *  coordinator calls this every tick the NPC is inside the decoy's radius. */
	void DistractTo(AActor* Decoy, float Seconds);

	/** True while a valid distraction is still running. */
	bool IsDistracted() const;

	/** What is distracting this NPC, or null. */
	AActor* GetDistraction() const;

	/** Drop the distraction. Perception then wins again with whatever it last asked for, and if that
	 *  is nothing the sense task starts looking for somebody, which is what stops the NPC standing
	 *  there shooting a spent prop. */
	void EndDistraction();

	// ==================== Siege fallback ====================
	// On a map with a siege core (ABuildableActor::IsSiegeCore) the answer for "nobody is visible" is the base's core. It
	// sits UNDER every intent — perception, script and distraction keep their own order and the core
	// only speaks when none of them names a target — which is the tower-defence rule: a visible
	// player matters, the march on the core is the default.
	//
	// The walker spawned at the fog line is the case this exists for: an NPC that has never seen a
	// player would otherwise stand where it spawned for the whole siege.

	/** The core being marched on, or null when the map has none or the last one fell. Re-found on a
	 *  slow cadence; a world holds one or two cores, so the walk is cheap. */
	TWeakObjectPtr<ABuildableActor> SiegeCore;

	/** How often SiegeCore is re-found. */
	UPROPERTY(EditAnywhere, Category = "Shooter|Siege", meta = (Units = "s"))
	float SiegeCoreRefreshInterval = 1.0f;

	/** Counts down to the next re-find. */
	float SiegeCoreRefreshTimer = 0.0f;

	/** Re-find the nearest live core of this world. */
	void RefreshSiegeCore();

protected:

	/** Called when the AI perception component updates a perception on a given actor */
	UFUNCTION()
	void OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	/** Called when the AI perception component forgets a given actor */
	UFUNCTION()
	void OnPerceptionForgotten(AActor* Actor);

public:
	/** Force perception system to update immediately (use after respawn) */
	UFUNCTION(BlueprintCallable, Category = "AI|Perception")
	void ForcePerceptionUpdate();

	// IGenericTeamAgentInterface
	virtual FGenericTeamId GetGenericTeamId() const override { return TeamId; }
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override { TeamId = NewTeamId; }

	/** Is Other somebody this NPC fights? The single answer for "friend or foe", replacing the
	 *  "does the actor carry the Player tag" test the sense task used to run.
	 *
	 *  A tag test only ever had one answer, so it could not survive a second enemy faction: an NPC
	 *  of faction 2 carries no Player tag and would be invisible to faction 1. Team attitude asks
	 *  the actual question instead, and it already knows about players (team 0) and enemies
	 *  (team 1) because both sides carry a team id today.
	 *
	 *  Attitude is asked FROM the controller (the perception listener) TO the actor, because that
	 *  is the direction the engine itself uses: FGenericTeamId::GetTeamIdentifier casts the actor
	 *  with no fallback to its controller, so the target side must answer through its pawn. */
	UFUNCTION(BlueprintPure, Category = "AI|Team")
	bool IsHostileTo(const AActor* Other) const;

	/** Tell this NPC's whole side where an enemy is. The "broadcast-contact" verb.
	 *
	 *  Called automatically on every sight edge (see OnPerceptionUpdated), so behaviour only needs to
	 *  call it for things perception cannot see for itself: a shot arriving from off-screen, a squad
	 *  order naming somebody, a scripted reveal.
	 *
	 *  Deliberately shares the OBSERVATION and not the decision. Everyone on the side learns a
	 *  position; each of them still chooses what to do with it, which is what keeps the enemies
	 *  reading as people who talk to each other rather than as one mind with many bodies. */
	void BroadcastContactAt(AActor* Enemy, const FVector& Location, bool bSeenNow);

	/** Same, at the enemy's current position, and reachable from Blueprint. Separate name rather than
	 *  an overload because UnrealHeaderTool does not allow overloaded UFUNCTIONs. */
	UFUNCTION(BlueprintCallable, Category = "AI|Team")
	void BroadcastContact(AActor* Enemy);

protected:
	/** Broadcast detected enemy to nearby teammates via Team Sense */
	void BroadcastEnemyToTeam(AActor* DetectedEnemy, const FVector& LastKnownLocation);
};