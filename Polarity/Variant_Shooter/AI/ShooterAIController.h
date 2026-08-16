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

	/** Enemy currently being targeted */
	TObjectPtr<AActor> TargetEnemy;

	/** What is currently holding this NPC's attention against its own perception, and until when.
	 *  Weak: the decoy is a prop and can be destroyed mid-distraction, and that has to end the lock
	 *  rather than leave the NPC staring at nothing. Server-side state, like everything the AI
	 *  decides. */
	TWeakObjectPtr<AActor> Distraction;
	float DistractionEndTime = 0.0f;

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

	/** Pawn initialization */
	virtual void OnPossess(APawn* InPawn) override;

protected:

	/** Called when the possessed pawn dies */
	UFUNCTION()
	void OnPawnDeath(AShooterNPC* DeadNPC);

public:

	/** Sets the targeted enemy.
	 *
	 *  Refused while a distraction holds this NPC (see DistractTo): perception keeps firing at the
	 *  players it can see, and without the refusal it re-pointed the NPC at one of them on the very
	 *  next stimulus, which is a decoy that visibly does nothing. */
	void SetCurrentTarget(AActor* Target);

	/** Clears the targeted enemy */
	void ClearCurrentTarget();

	/** Returns the targeted enemy */
	AActor* GetCurrentTarget() const { return TargetEnemy; };

	// ==================== Distraction ====================
	// A decoy is not something this NPC senses; it is something told to it. The coordinator owns the
	// decision (it is the one place that knows every registered NPC and who each is fighting) and
	// this is where it lands, because the behaviour tree reads the CONTROLLER's target, not the
	// coordinator's — the same route AArenaManager uses when it makes an arena aggro.

	/** Look at Decoy and refuse to look away for Seconds. Refreshing it with a new deadline is
	 *  normal: the coordinator calls this every tick the NPC is inside the decoy's radius. */
	void DistractTo(AActor* Decoy, float Seconds);

	/** True while a valid distraction is still running. */
	bool IsDistracted() const;

	/** What is distracting this NPC, or null. */
	AActor* GetDistraction() const;

	/** Drop the lock, and drop the decoy as a target with it, so perception starts looking for a
	 *  player again instead of leaving the NPC shooting a spent prop. */
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