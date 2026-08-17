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

protected:
	/** Broadcast detected enemy to nearby teammates via Team Sense */
	void BroadcastEnemyToTeam(AActor* DetectedEnemy, const FVector& LastKnownLocation);
};